// FreeArc-ex GUI: GTK3 (gtkmm 3.24) front end over the same core the CLI uses.
// Milestone M5, first slice: open an .arc, list its contents, extract it.
#include <gtkmm.h>

#include <filesystem>
#include <memory>
#include <string>

#include "fa/arc.h"

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
    void refresh_status();

    Gtk::Box box_{Gtk::ORIENTATION_VERTICAL};
    Gtk::MenuBar menu_;
    Gtk::ScrolledWindow scroll_;
    Gtk::TreeView view_;
    Gtk::Statusbar status_;

    EntryModelColumns columns_;
    Glib::RefPtr<Gtk::ListStore> model_;

    std::unique_ptr<fa::Archive> archive_;
    std::string archive_path_;
};

MainWindow::MainWindow()
{
    set_title("FreeArc-ex");
    set_default_size(720, 480);

    // File menu.
    auto* file_item = Gtk::manage(new Gtk::MenuItem("_File", true));
    Gtk::Menu* file_menu = Gtk::manage(new Gtk::Menu());
    {
        auto* open_item = Gtk::manage(new Gtk::MenuItem("_Open archive...", true));
        open_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_open));
        file_menu->append(*open_item);

        auto* quit_item = Gtk::manage(new Gtk::MenuItem("_Quit", true));
        quit_item->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_quit));
        file_menu->append(*quit_item);
    }
    file_item->set_submenu(*file_menu);
    menu_.append(*file_item);

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

    // Listing.
    model_ = Gtk::ListStore::create(columns_);
    view_.set_model(model_);
    view_.append_column("Name", columns_.name);
    view_.append_column("Size", columns_.size);
    view_.append_column("Date", columns_.date);
    view_.set_headers_visible(true);

    scroll_.add(view_);
    scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    scroll_.set_vexpand(true);

    box_.pack_start(menu_, Gtk::PACK_SHRINK);
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
