// FreeArc-ex GUI: GTK3 (gtkmm 3.24) front end over the same core the CLI uses.
// Modern layout: header bar, searchable archive listing with icons, details
// panel and the archive operations (create, update, test, extract).
#include <gtkmm.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <fstream>
#include <cstdlib>

#include "fa/arc.h"
#include "fa/arc_writer.h"
#include "fa/crypto.h"
#include "fa/ops.h"
#include "fa/update.h"

namespace {

class EntryModelColumns : public Gtk::TreeModel::ColumnRecord {
public:
    EntryModelColumns()
    {
        add(icon);
        add(directory);
        add(size);
        add(date);
        add(name);
    }

    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> icon;
    Gtk::TreeModelColumn<bool> directory;
    Gtk::TreeModelColumn<std::string> size;
    Gtk::TreeModelColumn<std::string> date;
    Gtk::TreeModelColumn<std::string> name;
};

class DiskColumns : public Gtk::TreeModel::ColumnRecord {
public:
    DiskColumns()
    {
        add(icon);
        add(directory);
        add(name);
        add(path);
    }

    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> icon;
    Gtk::TreeModelColumn<bool> directory;
    Gtk::TreeModelColumn<std::string> name;
    Gtk::TreeModelColumn<std::string> path;
};

std::filesystem::path config_file()
{
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : ".") / ".config" / "freearc-ex" / "gui.ini";
}

struct UiConfig {
    int width = 980;
    int height = 620;
    std::string last_dir;
};

UiConfig load_config()
{
    UiConfig config;
    std::ifstream in(config_file());
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "width") config.width = std::atoi(value.c_str());
        else if (key == "height") config.height = std::atoi(value.c_str());
        else if (key == "dir") config.last_dir = value;
    }
    return config;
}

void save_config(const UiConfig& config)
{
    std::error_code ec;
    std::filesystem::create_directories(config_file().parent_path(), ec);
    std::ofstream out(config_file(), std::ios::trunc);
    out << "width=" << config.width << "\n";
    out << "height=" << config.height << "\n";
    out << "dir=" << config.last_dir << "\n";
}

std::string format_size(std::uint64_t bytes)
{
    if (bytes >= 1024ull * 1024 * 1024) {
        return Glib::ustring::format(std::fixed, std::setprecision(1),
                                     static_cast<double>(bytes) / (1024.0 * 1024 * 1024)) +
               " GiB";
    }
    if (bytes >= 1024ull * 1024) {
        return Glib::ustring::format(std::fixed, std::setprecision(1),
                                     static_cast<double>(bytes) / (1024.0 * 1024)) +
               " MiB";
    }
    if (bytes >= 1024) {
        return Glib::ustring::format(std::fixed, std::setprecision(1),
                                     static_cast<double>(bytes) / 1024.0) +
               " KiB";
    }
    return std::to_string(bytes) + " B";
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
    void build_ui();
    void on_open();
    void on_new_archive();
    void on_add_folder();
    void on_extract();
    void on_test();
    void on_about();
    void on_quit();
    void refresh();
    void refresh_status();
    void on_selection_changed();
    void on_search_changed();
    void set_details(const Glib::ustring& name, const Glib::ustring& size,
                     const Glib::ustring& date);
    void clear_details();
    void refresh_disk();
    void navigate_disk(const std::string& dir);
    void add_paths(const std::vector<std::string>& paths);

    void run_operation(const std::string& title,
                       const std::function<bool(const fa::ProgressFn&, std::string&)>& operation);

    // Header bar and controls.
    Gtk::HeaderBar header_;
    Gtk::Button new_button_{"New"};
    Gtk::Button open_button_{"Open"};
    Gtk::Button add_button_{"Add"};
    Gtk::Button extract_button_{"Extract"};
    Gtk::Button test_button_{"Test"};
    Gtk::MenuButton menu_button_;
    Glib::RefPtr<Gio::Menu> menu_model_;

    // Layout.
    Gtk::Box box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::SearchEntry search_;
    Gtk::Paned paned_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::ScrolledWindow scroll_;
    Gtk::TreeView view_;

    // Details panel.
    Gtk::Box details_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Label details_title_;
    Gtk::Label details_size_;
    Gtk::Label details_date_;

    // Disk pane (left): browse a folder; drag a selection onto the archive to
    // add it.
    Gtk::Box disk_box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::Box disk_toolbar_{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Button disk_up_{"Up"};
    Gtk::Entry disk_path_;
    Gtk::ScrolledWindow disk_scroll_;
    Gtk::TreeView disk_view_;
    DiskColumns disk_columns_;
    Glib::RefPtr<Gtk::ListStore> disk_model_;
    std::string disk_dir_;

    Gtk::Statusbar status_;

    EntryModelColumns columns_;
    Glib::RefPtr<Gtk::ListStore> model_;
    Glib::RefPtr<Gtk::TreeModelFilter> filter_;

    std::unique_ptr<fa::Archive> archive_;
    std::string archive_path_;
    UiConfig config_;

    bool on_key_press(GdkEventKey* event);

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
    config_ = load_config();
    set_title("FreeArc-ex");
    set_default_size(config_.width, config_.height);
    signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_key_press), false);
    signal_delete_event().connect([this](GdkEventAny*) {
        config_.width = get_width();
        config_.height = get_height();
        config_.last_dir = disk_dir_;
        save_config(config_);
        return false;  // let the default handler close the window
    });
    build_ui();
    refresh_status();
    status_.push("Open an archive or create a new one");
}

void MainWindow::build_ui()
{
    // A light CSS layer: rounded header, comfortable rows and a subtle details
    // card. Everything degrades gracefully if the theme does not support it.
    auto css = Gtk::CssProvider::create();
    css->load_from_data(
        "headerbar { min-height: 44px; }\n"
        "treeview.view { padding: 2px; }\n"
        "treeview.view:selected { border-radius: 6px; }\n"
        ".details-card { background-color: alpha(currentColor, 0.04); border-radius: 10px; "
        "padding: 14px; }\n"
        ".details-title { font-weight: bold; font-size: 1.1em; }\n"
        ".dim-label { opacity: 0.65; }\n");
    Gtk::StyleContext::add_provider_for_screen(Gdk::Screen::get_default(), css,
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Header bar.
    set_titlebar(header_);
    header_.set_show_close_button(true);
    header_.pack_start(new_button_);
    header_.pack_start(open_button_);
    header_.pack_start(add_button_);
    header_.pack_start(extract_button_);
    header_.pack_start(test_button_);
    header_.set_custom_title(*Gtk::manage(new Gtk::Label("FreeArc-ex")));

    auto menu = Gio::Menu::create();
    menu->append("_About", "app.about");
    menu->append("_Quit", "app.quit");
    menu_model_ = menu;
    menu_button_.set_menu_model(menu_model_);
    menu_button_.set_image_from_icon_name("open-menu-symbolic");
    header_.pack_end(menu_button_);

    // Actions for the menu button.
    auto about_action = Gio::SimpleAction::create("about");
    about_action->signal_activate().connect([this](const Glib::VariantBase&) { on_about(); });
    add_action(about_action);
    auto quit_action = Gio::SimpleAction::create("quit");
    quit_action->signal_activate().connect([this](const Glib::VariantBase&) { on_quit(); });
    add_action(quit_action);

    new_button_.set_tooltip_text("Create a new archive");
    open_button_.set_tooltip_text("Open an archive");
    add_button_.set_tooltip_text("Add a folder to the open archive");
    extract_button_.set_tooltip_text("Extract the archive or the selected files");
    test_button_.set_tooltip_text("Test the archive integrity");
    new_button_.set_image_from_icon_name("document-new-symbolic");
    open_button_.set_image_from_icon_name("document-open-symbolic");
    add_button_.set_image_from_icon_name("list-add-symbolic");
    extract_button_.set_image_from_icon_name("document-save-symbolic");
    test_button_.set_image_from_icon_name("dialog-ok-symbolic");
    new_button_.set_always_show_image(true);
    open_button_.set_always_show_image(true);
    add_button_.set_always_show_image(true);
    extract_button_.set_always_show_image(true);
    test_button_.set_always_show_image(true);

    new_button_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_new_archive));
    open_button_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_open));
    add_button_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_add_folder));
    extract_button_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_extract));
    test_button_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_test));

    // Search + listing.
    search_.set_placeholder_text("Filter by name...");
    search_.set_margin_top(8);
    search_.set_margin_bottom(4);
    search_.set_margin_start(10);
    search_.set_margin_end(10);
    search_.signal_search_changed().connect(sigc::mem_fun(*this, &MainWindow::on_search_changed));

    model_ = Gtk::ListStore::create(columns_);
    filter_ = Gtk::TreeModelFilter::create(model_);
    filter_->set_visible_func([this](const Gtk::TreeModel::const_iterator& iter) {
        const Glib::ustring query = search_.get_text();
        if (query.empty()) return true;
        const std::string name = (*iter)[columns_.name];
        return Glib::ustring(name).lowercase().find(query.lowercase()) != Glib::ustring::npos;
    });

    view_.set_model(filter_);
    view_.set_headers_visible(true);
    view_.set_enable_search(false);

    int icon_width = 0;
    view_.append_column("", columns_.icon);
    auto* icon_renderer = dynamic_cast<Gtk::CellRendererPixbuf*>(
        view_.get_column(0)->get_first_cell());
    (void)icon_width;
    (void)icon_renderer;
    view_.get_column(0)->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    view_.get_column(0)->set_fixed_width(32);

    view_.append_column("Name", columns_.name);
    view_.append_column("Size", columns_.size);
    view_.append_column("Date", columns_.date);
    view_.get_column(1)->set_expand(true);
    view_.get_column(1)->set_sort_column(columns_.name);
    view_.get_column(2)->set_sort_column(columns_.size);
    view_.get_column(3)->set_sort_column(columns_.date);
    view_.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);
    view_.get_selection()->signal_changed().connect(
        sigc::mem_fun(*this, &MainWindow::on_selection_changed));
    view_.signal_row_activated().connect(
        [this](const Gtk::TreeModel::Path&, Gtk::TreeViewColumn*) { on_extract(); });

    scroll_.add(view_);
    scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll_.set_vexpand(true);

    // Details panel.
    details_.set_margin_top(10);
    details_.set_margin_bottom(10);
    details_.set_margin_start(10);
    details_.set_margin_end(10);
    details_.set_spacing(6);
    auto* card = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 6));
    card->get_style_context()->add_class("details-card");

    details_title_.get_style_context()->add_class("details-title");
    details_title_.set_halign(Gtk::ALIGN_START);
    details_title_.set_line_wrap(true);
    details_size_.get_style_context()->add_class("dim-label");
    details_size_.set_halign(Gtk::ALIGN_START);
    details_date_.get_style_context()->add_class("dim-label");
    details_date_.set_halign(Gtk::ALIGN_START);
    card->pack_start(details_title_, Gtk::PACK_SHRINK);
    card->pack_start(details_size_, Gtk::PACK_SHRINK);
    card->pack_start(details_date_, Gtk::PACK_SHRINK);
    details_.pack_start(*card, Gtk::PACK_SHRINK);
    details_.pack_start(*Gtk::manage(new Gtk::Label()), Gtk::PACK_EXPAND_WIDGET);

    // Disk pane: browse a folder and drag it onto the archive.
    disk_toolbar_.set_spacing(6);
    disk_toolbar_.set_margin_top(4);
    disk_toolbar_.set_margin_start(6);
    disk_toolbar_.set_margin_end(6);
    disk_path_.set_hexpand(true);
    disk_path_.set_placeholder_text("Folder to browse");
    disk_toolbar_.pack_start(disk_up_, Gtk::PACK_SHRINK);
    disk_toolbar_.pack_start(disk_path_, Gtk::PACK_EXPAND_WIDGET);

    disk_model_ = Gtk::ListStore::create(disk_columns_);
    disk_view_.set_model(disk_model_);
    disk_view_.set_headers_visible(false);
    disk_view_.append_column("", disk_columns_.icon);
    disk_view_.append_column("Name", disk_columns_.name);
    disk_view_.get_column(0)->set_fixed_width(30);
    disk_view_.get_column(1)->set_expand(true);
    disk_view_.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);
    disk_view_.signal_row_activated().connect([this](const Gtk::TreeModel::Path& path,
                                                     Gtk::TreeViewColumn*) {
        auto row = *disk_model_->get_iter(path);
        if (!row[disk_columns_.directory]) return;
        navigate_disk(row[disk_columns_.path]);
    });
    disk_view_.enable_model_drag_source();
    std::vector<Gtk::TargetEntry> targets;
    targets.push_back(Gtk::TargetEntry("text/uri-list"));
    disk_view_.drag_source_set(targets, Gdk::MODIFIER_MASK, Gdk::ACTION_COPY);
    disk_view_.signal_drag_data_get().connect(
        [this](const Glib::RefPtr<Gdk::DragContext>&, Gtk::SelectionData& data, guint, guint) {
            std::string uris;
            for (const auto& path : disk_view_.get_selection()->get_selected_rows()) {
                auto row = *disk_model_->get_iter(path);
                if (!uris.empty()) uris += "\r\n";
                uris += Glib::filename_to_uri(row[disk_columns_.path]);
            }
            data.set_uris(std::vector<Glib::ustring>{uris});
        });
    disk_up_.signal_clicked().connect([this]() {
        navigate_disk(std::filesystem::path(disk_dir_).parent_path().string());
    });
    disk_path_.signal_activate().connect([this]() { navigate_disk(disk_path_.get_text()); });

    disk_scroll_.add(disk_view_);
    disk_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    disk_scroll_.set_vexpand(true);
    disk_box_.pack_start(disk_toolbar_, Gtk::PACK_SHRINK);
    disk_box_.pack_start(disk_scroll_, Gtk::PACK_EXPAND_WIDGET);

    paned_.pack1(disk_box_, true, true);
    paned_.pack2(scroll_, true, true);
    paned_.set_position(320);

    // Dropping files/folders on the archive pane adds them to the open archive.
    view_.drag_dest_set(targets, Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_COPY);
    view_.signal_drag_data_received().connect(
        [this](const Glib::RefPtr<Gdk::DragContext>& context, int, int,
               const Gtk::SelectionData& data, guint, guint time) {
            std::vector<std::string> paths;
            for (const auto& uri : data.get_uris()) {
                const std::string path = Glib::filename_from_uri(uri);
                if (!path.empty()) paths.push_back(path);
            }
            if (paths.empty() || archive_path_.empty()) {
                context->drag_finish(false, false, time);
                return;
            }
            add_paths(paths);
            context->drag_finish(true, false, time);
        });

    navigate_disk(config_.last_dir.empty() ? Glib::get_current_dir() : config_.last_dir);

    box_.pack_start(search_, Gtk::PACK_SHRINK);
    box_.pack_start(paned_, Gtk::PACK_EXPAND_WIDGET);
    box_.pack_end(status_, Gtk::PACK_SHRINK);
    add(box_);

    clear_details();
    show_all_children();
}

void MainWindow::navigate_disk(const std::string& dir)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) return;
    disk_dir_ = std::filesystem::absolute(dir).lexically_normal().string();
    config_.last_dir = disk_dir_;
    disk_path_.set_text(disk_dir_);

    disk_model_->clear();
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(disk_dir_, ec)) {
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        const bool a_dir = a.is_directory();
        const bool b_dir = b.is_directory();
        if (a_dir != b_dir) return a_dir;
        return a.path().filename().string() < b.path().filename().string();
    });
    for (const auto& entry : entries) {
        auto row = *disk_model_->append();
        const bool is_dir = entry.is_directory(ec);
        row[disk_columns_.name] = entry.path().filename().string();
        row[disk_columns_.directory] = is_dir;
        row[disk_columns_.path] = entry.path().string();
        row[disk_columns_.icon] =
            Gtk::IconTheme::get_default()->load_icon(is_dir ? "folder" : "text-x-generic", 16);
    }
}

void MainWindow::refresh_disk()
{
    if (!disk_dir_.empty()) navigate_disk(disk_dir_);
}

void MainWindow::add_paths(const std::vector<std::string>& paths)
{
    if (archive_path_.empty()) {
        Gtk::MessageDialog dialog(*this, "Open or create an archive first", false,
                                  Gtk::MESSAGE_INFO);
        dialog.run();
        return;
    }
    run_operation("Add to archive",
                  [this, paths](const fa::ProgressFn& progress, std::string& error) {
                      fa::AddOptions add;
                      add.method = "lzma";
                      add.progress = progress;
                      std::vector<std::filesystem::path> inputs;
                      for (const auto& path : paths) inputs.emplace_back(path);
                      if (!fa::update_archive(archive_path_, inputs, fa::UpdateMode::Update, add,
                                              error)) {
                          return false;
                      }
                      open(archive_path_);
                      return true;
                  });
}

void MainWindow::set_details(const Glib::ustring& name, const Glib::ustring& size,
                             const Glib::ustring& date)
{
    details_title_.set_text(name);
    details_size_.set_text(size);
    details_date_.set_text(date);
}

void MainWindow::clear_details()
{
    details_title_.set_text("No selection");
    details_size_.set_text("");
    details_date_.set_text("");
}

void MainWindow::on_selection_changed()
{
    const auto rows = view_.get_selection()->get_selected_rows();
    if (rows.empty()) {
        clear_details();
        return;
    }
    if (rows.size() > 1) {
        status_.push(std::to_string(rows.size()) + " entries selected");
        return;
    }
    auto iter = filter_->get_iter(rows.front());
    const auto& row = *iter;
    const std::string name = row[columns_.name];
    const std::string size = row[columns_.size];
    const std::string date = row[columns_.date];
    set_details(name, size, date);
    status_.push(name + " — " + size + " — " + date);
}

void MainWindow::on_search_changed()
{
    filter_->refilter();
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
    refresh();

    set_title("FreeArc-ex — " + std::filesystem::path(path).filename().string());
    refresh_status();
}

void MainWindow::refresh()
{
    model_->clear();
    if (!archive_) return;

    for (const auto& entry : archive_->info().files) {
        auto row = *model_->append();
        row[columns_.name] = entry.is_dir ? entry.name + "/" : entry.name;
        row[columns_.size] = entry.is_dir ? "-" : format_size(entry.size);
        row[columns_.date] = format_date(entry.mtime);
        row[columns_.directory] = entry.is_dir;
        row[columns_.icon] = Gtk::IconTheme::get_default()->load_icon(
            entry.is_dir ? "folder" : "text-x-generic", 16);
    }
    clear_details();
}

void MainWindow::run_operation(
    const std::string& title, const std::function<bool(const fa::ProgressFn&, std::string&)>& operation)
{
    auto state = std::make_shared<ProgressState>();
    auto dialog = std::make_shared<Gtk::Dialog>(title, *this, true);
    dialog->set_default_size(460, -1);
    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 10));
    box->set_margin_top(16);
    box->set_margin_bottom(16);
    box->set_margin_start(16);
    box->set_margin_end(16);
    auto* label = Gtk::manage(new Gtk::Label("Working..."));
    label->set_halign(Gtk::ALIGN_START);
    label->set_ellipsize(Pango::ELLIPSIZE_MIDDLE);
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

    auto done_once = std::make_shared<bool>(false);
    Glib::signal_timeout().connect(
        [state, bar, label, dialog, done_once, this]() {
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
            if (!*done_once) {
                *done_once = true;
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

bool MainWindow::on_key_press(GdkEventKey* event)
{
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    if (ctrl) {
        switch (event->keyval) {
            case GDK_KEY_o: on_open(); return true;
            case GDK_KEY_n: on_new_archive(); return true;
            case GDK_KEY_e: on_extract(); return true;
            case GDK_KEY_t: on_test(); return true;
            case GDK_KEY_q: on_quit(); return true;
            default: break;
        }
    }
    if (event->keyval == GDK_KEY_F5) {
        on_test();
        return true;
    }
    return false;
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

    Gtk::Dialog options("Compression options", *this, true);
    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(10);
    grid->set_column_spacing(10);
    grid->set_margin_top(16);
    grid->set_margin_bottom(16);
    grid->set_margin_start(16);
    grid->set_margin_end(16);

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

    auto* solid_label = Gtk::manage(new Gtk::Label("Solid blocks:"));
    solid_label->set_halign(Gtk::ALIGN_START);
    auto* solid = Gtk::manage(new Gtk::ComboBoxText());
    for (const char* item : {"all together", "one file per block", "2 files", "8 files"}) {
        solid->append(item);
    }
    solid->set_active(0);

    auto* recovery_label = Gtk::manage(new Gtk::Label("Recovery record:"));
    recovery_label->set_halign(Gtk::ALIGN_START);
    auto* recovery = Gtk::manage(new Gtk::Entry());
    recovery->set_placeholder_text("(none, e.g. 4%)");

    auto* encrypt_headers = Gtk::manage(new Gtk::CheckButton("Encrypt the header and directory (-hp)"));
    encrypt_headers->set_halign(Gtk::ALIGN_START);

    grid->attach(*method_label, 0, 0, 1, 1);
    grid->attach(*methods, 1, 0, 1, 1);
    grid->attach(*password_label, 0, 1, 1, 1);
    grid->attach(*password, 1, 1, 1, 1);
    grid->attach(*solid_label, 0, 2, 1, 1);
    grid->attach(*solid, 1, 2, 1, 1);
    grid->attach(*recovery_label, 0, 3, 1, 1);
    grid->attach(*recovery, 1, 3, 1, 1);
    grid->attach(*encrypt_headers, 1, 4, 1, 1);
    options.get_content_area()->add(*grid);
    options.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    options.add_button("_Create", Gtk::RESPONSE_OK);
    options.show_all();
    if (options.run() != Gtk::RESPONSE_OK) return;

    std::string method = methods->get_active_text();
    if (method.rfind("0 (", 0) == 0) method = "storing";
    const std::string pass = password->get_text();
    const int solid_index = solid->get_active_row_number();
    const std::string recovery_text = recovery->get_text();
    const bool hp = encrypt_headers->get_active() && !pass.empty();

    run_operation("Create archive", [this, archive, input, method, pass, solid_index, recovery_text,
                                     hp](const fa::ProgressFn& p, std::string& error) {
        fa::AddOptions add;
        add.method = method;
        add.progress = p;
        add.recovery = recovery_text;
        add.recovery_present = !recovery_text.empty();
        add.header_encryption = hp;
        if (solid_index == 1) add.solid = {{fa::SolidRule::Kind::None, 0}};
        else if (solid_index == 2) add.solid = {{fa::SolidRule::Kind::Files, 2}};
        else if (solid_index == 3) add.solid = {{fa::SolidRule::Kind::Files, 8}};
        if (!pass.empty()) fa::set_password(pass);
        if (!fa::create_archive(archive, {input}, add, error)) return false;
        open(archive);
        return true;
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

    run_operation("Update archive",
                  [this, input](const fa::ProgressFn& progress, std::string& error) {
                      fa::AddOptions add;
                      add.method = "lzma";
                      add.progress = progress;
                      if (!fa::update_archive(archive_path_, {input}, fa::UpdateMode::Update, add,
                                              error)) {
                          return false;
                      }
                      open(archive_path_);
                      return true;
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
        auto row = *filter_->get_iter(path);
        const std::string name = row[columns_.name];
        if (!name.empty() && name.back() != '/') masks.push_back(name);
    }

    Gtk::Dialog dialog("Extract", *this, true);
    auto* grid = Gtk::manage(new Gtk::Grid());
    grid->set_row_spacing(10);
    grid->set_column_spacing(10);
    grid->set_margin_top(16);
    grid->set_margin_bottom(16);
    grid->set_margin_start(16);
    grid->set_margin_end(16);

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
    selected_label->get_style_context()->add_class("dim-label");

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

void MainWindow::on_quit()
{
    close();
}

}  // namespace

int main(int argc, char** argv)
{
    auto app = Gtk::Application::create("org.freearc.ex");
    MainWindow window;
    if (argc > 1) window.open(argv[1]);
    return app->run(window);
}
