// FreeArc-ex GUI: GTK3 (gtkmm 3.24) front end over the same core the CLI uses.
// Milestone M5, first slice: open an .arc, list its contents, extract it.
#include <gtkmm.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "fa/arc.h"
#include "fa/ops.h"
#include "fa/arc_writer.h"
#include "fa/update.h"
#include "fa/crypto.h"

namespace {

// Row of the archive listing.
class EntryModelColumns : public Gtk::TreeModel::ColumnRecord {
public:
    EntryModelColumns()
    {
        add(directory);
        add(size);
        add(date);
        add(name);
    }

    Gtk::TreeModelColumn<bool> directory;
    Gtk::TreeModelColumn<std::string> size;
    Gtk::TreeModelColumn<std::string> date;
    Gtk::TreeModelColumn<std::string> name;
};

std::string format_size(std::uint64_t bytes)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(bytes));
    return buffer;
}

std::string format_date(std::int32_t seconds)
{
    if (seconds == 0) return "-";
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
#if defined(_WIN32)
    if (localtime_s(&tm, &t) != 0) return "-";
#else
    if (localtime_r(&t, &tm) == nullptr) return "-";
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &tm);
    return buffer;
}

class MainWindow : public Gtk::ApplicationWindow {
public:
    MainWindow();

    void open(const std::string& path);

private:
    void on_open();
    void on_quit();
    void on_about();
    void on_test();
    void on_extract();
    void on_new_archive();
    void on_add_folder();
    void refresh_status();

    // Runs an archive operation in a worker thread with a progress dialog.
    // `operation` receives a progress callback and returns true on success.
    void run_operation(const std::string& title,
                       const std::function<bool(const fa::ProgressFn&, std::string&)>& operation);

    Gtk::Box box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::MenuBar menu_;
    Gtk::ScrolledWindow scroll_;
    Gtk::TreeView view_;
    Gtk::Statusbar status_;

    EntryModelColumns columns_;
    Glib::RefPtr<Gtk::ListStore> model_;

    std::unique_ptr<fa::Archive> archive_;
    std::string archive_path_;

    // Shared with the worker thread while an operation runs.
    struct ProgressState {
        std::atomic<std::uint64_t> done{0};
        std::atomic<std::uint64_t> total{0};
        std::atomic<bool> cancel{false};
        std::atomic<bool> finished{false};
        std::atomic<bool> ok{false};
        std::mutex mutex;
        std::string current;
        std::string error;
    };
};

MainWindow::MainWindow()
{
    set_title("FreeArc-ex");
    set_default_size(720, 480);

    // File menu.
    auto* file_item = Gtk::manage(new Gtk::MenuItem("_File", true));
    Gtk::Menu* file_menu = Gtk::manage(new Gtk::Menu());
    {
        auto* new_item = Gtk::manage(new Gtk::MenuItem("_New archive...", true));
        new_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_new_archive));
        file_menu->append(*new_item);

        auto* open_item = Gtk::manage(new Gtk::MenuItem("_Open archive...", true));
        open_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_open));
        file_menu->append(*open_item);

        auto* quit_item = Gtk::manage(new Gtk::MenuItem("_Quit", true));
        quit_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_quit));
        file_menu->append(*quit_item);
    }
    file_item->set_submenu(*file_menu);
    menu_.append(*file_item);

    // Actions menu.
    auto* actions_item = Gtk::manage(new Gtk::MenuItem("_Actions", true));
    Gtk::Menu* actions_menu = Gtk::manage(new Gtk::Menu());
    {
        auto* test_item = Gtk::manage(new Gtk::MenuItem("_Test archive", true));
        test_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_test));
        actions_menu->append(*test_item);

        auto* extract_item = Gtk::manage(new Gtk::MenuItem("_Extract to...", true));
        extract_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_extract));
        actions_menu->append(*extract_item);
    }
    actions_item->set_submenu(*actions_menu);
    menu_.append(*actions_item);

    // Help menu.
    auto* help_item = Gtk::manage(new Gtk::MenuItem("_Help", true));
    Gtk::Menu* help_menu = Gtk::manage(new Gtk::Menu());
    {
        auto* about_item = Gtk::manage(new Gtk::MenuItem("_About", true));
        about_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_about));
        help_menu->append(*about_item);
    }
    help_item->set_submenu(*help_menu);
    menu_.append(*help_item);

    // Toolbar.
    auto* toolbar = Gtk::manage(new Gtk::Toolbar());
    toolbar->set_toolbar_style(Gtk::ToolbarStyle::TOOLBAR_TEXT);
    {
        auto add_button = [&](const char* label, void (MainWindow::*handler)()) {
            auto* item = Gtk::manage(new Gtk::ToolButton(label));
            item->signal_clicked().connect(sigc::mem_fun(*this, handler));
            toolbar->append(*item);
        };
        add_button("New", &MainWindow::on_new_archive);
        add_button("Open", &MainWindow::on_open);
        add_button("Add...", &MainWindow::on_add_folder);
        add_button("Extract...", &MainWindow::on_extract);
        add_button("Test", &MainWindow::on_test);
        toolbar->append(*Gtk::manage(new Gtk::SeparatorToolItem()));
        add_button("Quit", &MainWindow::on_quit);
    }

    // Listing.
    model_ = Gtk::ListStore::create(columns_);
    view_.set_model(model_);
    view_.append_column("Name", columns_.name);
    view_.append_column("Size", columns_.size);
    view_.append_column("Date", columns_.date);
    view_.set_headers_visible(true);

    // Sortable columns and multiple selection.
    view_.get_column(0)->set_sort_column(columns_.name);
    view_.get_column(1)->set_sort_column(columns_.size);
    view_.get_column(2)->set_sort_column(columns_.date);
    view_.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);
    // Double click extracts the selection (or the whole archive).
    view_.signal_row_activated().connect([this](const Gtk::TreeModel::Path&, Gtk::TreeViewColumn*) {
        on_extract();
    });

    scroll_.add(view_);
    scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll_.set_vexpand(true);

    box_.pack_start(menu_, Gtk::PACK_SHRINK);
    box_.pack_start(*toolbar, Gtk::PACK_SHRINK);
    box_.pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
    box_.pack_end(status_, Gtk::PACK_SHRINK);
    add(box_);

    show_all_children();
    status_.push("Open an archive to see its contents");
}

void MainWindow::refresh_status()
{
    if (!archive_) {
        status_.push("No archive");
        return;
    }
    const auto& info = archive_->info();
    std::size_t files = 0;
    std::uint64_t bytes = 0;
    for (const auto& entry : info.files) {
        if (entry.is_dir) continue;
        ++files;
        bytes += entry.size;
    }
    status_.push(archive_path_ + " — " + std::to_string(files) + " file(s), " +
                 std::to_string(bytes) + " byte(s)");
}

void MainWindow::run_operation(
    const std::string& title, const std::function<bool(const fa::ProgressFn&, std::string&)>& operation)
{
    auto state = std::make_shared<ProgressState>();
    auto dialog = std::make_shared<Gtk::Dialog>(title, *this, true);
    dialog->set_default_size(420, -1);
    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 8));
    box->set_margin_top(12);
    box->set_margin_bottom(12);
    box->set_margin_start(12);
    box->set_margin_end(12);
    auto* label = Gtk::manage(new Gtk::Label("Working..."));
    label->set_halign(Gtk::ALIGN_START);
    auto* bar = Gtk::manage(new Gtk::ProgressBar());
    bar->set_show_text(true);
    box->pack_start(*label, Gtk::PACK_SHRINK);
    box->pack_start(*bar, Gtk::PACK_SHRINK);
    dialog->get_content_area()->add(*box);
    dialog->add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog->signal_response().connect([state, dialog](int response) {
        if (response == Gtk::RESPONSE_CANCEL) state->cancel = true;
        dialog->hide();
    });
    dialog->show_all();

    // Worker thread: runs the core operation and reports progress.
    std::thread worker([state, operation, this]() {
        std::string error;
        const bool ok = operation(
            [state](std::uint64_t done, std::uint64_t total, const std::string& current) {
                state->done = done;
                state->total = total;
                std::lock_guard<std::mutex> lock(state->mutex);
                state->current = current;
                return !state->cancel.load();
            },
            error);
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->error = error;
        }
        state->ok = ok;
        state->finished = true;
    });

    // UI polling.
    auto finished = std::make_shared<bool>(false);
    Glib::signal_timeout().connect(
        [state, bar, label, dialog, finished, this]() {
            const std::uint64_t total = state->total;
            const std::uint64_t done = state->done;
            if (total > 0) {
                bar->set_fraction(static_cast<double>(done) / static_cast<double>(total));
                bar->set_text(std::to_string(done * 100 / total) + "%");
            }
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (!state->current.empty()) label->set_text(state->current);
            }
            if (!state->finished) return true;

            dialog->hide();
            if (!*finished) {
                *finished = true;
                if (!state->ok && !state->cancel) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    Gtk::MessageDialog message(*this, state->error, false, Gtk::MESSAGE_ERROR);
                    message.run();
                }
                refresh_status();
            }
            return false;
        },
        100);

    dialog->run();
    state->cancel = true;
    worker.join();
}

void MainWindow::on_new_archive()
{
    Gtk::FileChooserDialog save(*this, "New archive", Gtk::FILE_CHOOSER_ACTION_SAVE);
    save.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    save.add_button("_Next", Gtk::RESPONSE_OK);
    save.set_do_overwrite_confirmation(true);
    save.set_current_name("archive.arc");
    if (save.run() != Gtk::RESPONSE_OK) return;
    std::string archive = save.get_filename();
    if (archive.size() < 4 || archive.substr(archive.size() - 4) != ".arc") archive += ".arc";

    Gtk::FileChooserDialog folder(*this, "Folder to add", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    folder.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    folder.add_button("_Next", Gtk::RESPONSE_OK);
    if (folder.run() != Gtk::RESPONSE_OK) return;
    const std::string input = folder.get_filename();

    // Compression options.
    Gtk::Dialog options("Compression options", *this, true);
    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(8);
    grid->set_column_spacing(8);
    grid->set_margin_top(12);
    grid->set_margin_bottom(12);
    grid->set_margin_start(12);
    grid->set_margin_end(12);

    auto* method_label = Gtk::manage(new Gtk::Label("Method:"));
    method_label->set_halign(Gtk::ALIGN_START);
    auto* methods = Gtk::manage(new Gtk::ComboBoxText());
    for (const char* item : {"lzma", "0 (storing)", "1", "2", "3", "4", "5", "9"}) {
        methods->append(item);
    }
    methods->set_active(0);

    auto* password_label = Gtk::manage(new Gtk::Label("Password:"));
    password_label->set_halign(Gtk::ALIGN_START);
    auto* password = Gtk::manage(new Gtk::Entry());
    password->set_visibility(false);
    password->set_placeholder_text("(none)");

    grid->attach(*method_label, 0, 0, 1, 1);
    grid->attach(*methods, 1, 0, 1, 1);
    grid->attach(*password_label, 0, 1, 1, 1);
    grid->attach(*password, 1, 1, 1, 1);
    options.get_content_area()->add(*grid);
    options.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    options.add_button("_Create", Gtk::RESPONSE_OK);
    options.show_all();
    if (options.run() != Gtk::RESPONSE_OK) return;

    std::string method = methods->get_active_text();
    if (method.rfind("0 (", 0) == 0) method = "storing";
    const std::string pass = password->get_text();

    run_operation("Create archive", [this, archive, input, method, pass](const fa::ProgressFn& p,
                                                                        std::string& error) {
        fa::AddOptions add;
        add.method = method;
        add.progress = p;
        if (!pass.empty()) fa::set_password(pass);
        if (!fa::create_archive(archive, {input}, add, error)) return false;
        // Show the new archive right away.
        open(archive);
        return true;
    });
}

void MainWindow::on_test()
{
    if (archive_path_.empty()) {
        Gtk::MessageDialog dialog(*this, "Open an archive first", false, Gtk::MESSAGE_INFO);
        dialog.run();
        return;
    }
    run_operation("Test archive", [this](const fa::ProgressFn& progress, std::string& error) {
        bool cancelled = false;
        return fa::test_archive(archive_path_, progress, cancelled, error);
    });
}

void MainWindow::on_extract()
{
    if (archive_path_.empty()) {
        Gtk::MessageDialog dialog(*this, "Open an archive first", false, Gtk::MESSAGE_INFO);
        dialog.run();
        return;
    }

    // Selected entries (if any) become include masks.
    std::vector<std::string> masks;
    for (const auto& path : view_.get_selection()->get_selected_rows()) {
        auto row = *model_->get_iter(path);
        const std::string name = row[columns_.name];
        if (!name.empty() && name.back() != '/') masks.push_back(name);
    }

    Gtk::Dialog dialog("Extract", *this, true);
    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(8);
    grid->set_column_spacing(8);
    grid->set_margin_top(12);
    grid->set_margin_bottom(12);
    grid->set_margin_start(12);
    grid->set_margin_end(12);

    auto* folder_label = Gtk::manage(new Gtk::Label("Destination:"));
    folder_label->set_halign(Gtk::ALIGN_START);
    auto* folder = Gtk::manage(new Gtk::FileChooserButton("Choose a folder",
                                                          Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER));
    folder->set_current_folder(Glib::get_current_dir());

    auto* policy_label = Gtk::manage(new Gtk::Label("If a file exists:"));
    policy_label->set_halign(Gtk::ALIGN_START);
    auto* policy = Gtk::manage(new Gtk::ComboBoxText());
    policy->append("overwrite");
    policy->append("keep existing");
    policy->set_active(0);

    auto* selected_label = Gtk::manage(new Gtk::Label(
        masks.empty() ? "Files: all" : "Files: " + std::to_string(masks.size()) + " selected"));
    selected_label->set_halign(Gtk::ALIGN_START);

    grid->attach(*folder_label, 0, 0, 1, 1);
    grid->attach(*folder, 1, 0, 1, 1);
    grid->attach(*policy_label, 0, 1, 1, 1);
    grid->attach(*policy, 1, 1, 1, 1);
    grid->attach(*selected_label, 0, 2, 2, 1);
    dialog.get_content_area()->add(*grid);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Extract", Gtk::RESPONSE_OK);
    dialog.show_all();
    if (dialog.run() != Gtk::RESPONSE_OK) return;

    const std::string destination = folder->get_filename();
    const bool overwrite = policy->get_active_row_number() == 0;

    run_operation("Extract archive",
                  [this, destination, masks, overwrite](const fa::ProgressFn& progress,
                                                        std::string& error) {
                      bool cancelled = false;
                      fa::ExtractOptions options;
                      options.output_dir = destination;
                      options.masks = masks;
                      options.overwrite = overwrite;
                      return fa::extract_archive(archive_path_, options, progress, cancelled,
                                                 error);
                  });
}

void MainWindow::on_add_folder()
{
    if (archive_path_.empty()) {
        Gtk::MessageDialog dialog(*this, "Open an archive first", false, Gtk::MESSAGE_INFO);
        dialog.run();
        return;
    }

    Gtk::FileChooserDialog folder(*this, "Folder to add or update", Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    folder.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    folder.add_button("_Add", Gtk::RESPONSE_OK);
    if (folder.run() != Gtk::RESPONSE_OK) return;
    const std::string input = folder.get_filename();

    run_operation("Update archive", [this, input](const fa::ProgressFn& progress,
                                                 std::string& error) {
        fa::AddOptions add;
        add.method = "lzma";
        add.progress = progress;
        if (!fa::update_archive(archive_path_, {input}, fa::UpdateMode::Update, add, error)) {
            return false;
        }
        open(archive_path_);  // refresh the listing
        return true;
    });
}

void MainWindow::open(const std::string& path)
{
    auto loaded = std::make_unique<fa::Archive>();
    std::string error;
    if (!fa::Archive::open(path, *loaded, error)) {
        Gtk::MessageDialog dialog(*this, error, false, Gtk::MESSAGE_ERROR);
        dialog.run();
        return;
    }

    archive_ = std::move(loaded);
    archive_path_ = path;
    model_->clear();

    for (const auto& entry : archive_->info().files) {
        auto row = *model_->append();
        row[columns_.name] = entry.is_dir ? entry.name + "/" : entry.name;
        row[columns_.size] = entry.is_dir ? "-" : format_size(entry.size);
        row[columns_.date] = format_date(entry.mtime);
        row[columns_.directory] = entry.is_dir;
    }

    set_title("FreeArc-ex — " + std::filesystem::path(path).filename().string());
    refresh_status();
}

void MainWindow::on_open()
{
    Gtk::FileChooserDialog dialog(*this, "Open archive", Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Open", Gtk::RESPONSE_OK);

    auto filter = Gtk::FileFilter::create();
    filter->set_name("FreeArc archives");
    filter->add_pattern("*.arc");
    dialog.add_filter(filter);
    filter = Gtk::FileFilter::create();
    filter->set_name("All files");
    filter->add_pattern("*");
    dialog.add_filter(filter);

    if (dialog.run() == Gtk::RESPONSE_OK) open(dialog.get_filename());
}

void MainWindow::on_quit()
{
    close();
}

void MainWindow::on_about()
{
    Gtk::AboutDialog dialog;
    dialog.set_program_name("FreeArc-ex");
    dialog.set_version("0.1.0");
    dialog.set_comments("Extended support for FreeArc 0.67.1 archives");
    dialog.set_license_type(Gtk::LICENSE_GPL_2_0);
    dialog.set_transient_for(*this);
    dialog.run();
}

}  // namespace

int main(int argc, char** argv)
{
    // The command line is handled here (a path opens it directly); passing argv
    // to Gtk::Application would make GIO try to open it as a document.
    auto app = Gtk::Application::create("org.freearc.ex");
    MainWindow window;
    // A path on the command line opens directly (handy for file managers).
    if (argc > 1) window.open(argv[1]);
    return app->run(window);
}
