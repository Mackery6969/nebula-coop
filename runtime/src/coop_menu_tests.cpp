// The online co-op main menu's model (galaxy/coop_menu.h): its rows, moving between them, what pressing does, and the
// Host page over the co-op saves (galaxy/coop_saves.h).
#include "galaxy/coop_menu.h"
#include "galaxy/coop_saves.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}

namespace fs = std::filesystem;

void write(const fs::path& path, const char* text) {
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void test_saves(const fs::path& scratch) {
    using galaxy::coop::SaveStore;
    SaveStore store(scratch / "coop_saves");
    expect(store.list().empty() && !store.converted(), "no saves, not converted, before the folder exists");

    // The old saves: files 1 and 3 of six, and the achievements shared by all of them.
    const fs::path nand = scratch / "nand_data";
    fs::create_directories(nand);
    write(nand / "GameData.bin", "old");
    write(scratch / "achievements.ini", "star1=2026\n");
    expect(store.convert(nand, 0b101u, scratch / "achievements.ini"), "the old files convert");
    expect(store.converted() && !store.convert(nand, 0b101u, {}), "they convert once");
    std::vector<galaxy::coop::SaveInfo> saves = store.list();
    expect(saves.size() == 2u, "one save per old file");
    expect(saves.size() == 2u && saves[0].name == "FILE 3" && saves[0].file == 3u && saves[1].file == 1u,
           "the newest first, each playing its old file");
    expect(read(store.folder("save1") / "GameData.bin") == "old" &&
               read(store.folder("save2") / "achievements.ini") == "star1=2026\n",
           "each a copy of the old saves and achievements");
    expect(read(nand / "GameData.bin") == "old", "the old saves stay as they were");

    const std::string fresh = store.create();
    expect(
        fresh == "save3" && store.find(fresh) && store.find(fresh)->name == "SAVE 3" && store.find(fresh)->file == 1u,
        "a new save, numbered after the others, plays file 1");
    expect(store.mark_played("save1", 100) && store.list()[0].id == "save1", "the last played comes first");
    expect(store.erase("save2") && !store.find("save2") && store.list().size() == 2u, "erased");
    expect(store.create() == "save4", "an erased number isn't reused while a later one remains");
    expect(!store.erase("../nand_data") && fs::exists(nand), "an id never leaves the saves folder");
    expect(!store.find("save9"), "a missing save isn't found");
    expect(store.rename("save1", "  My Save  ") && store.find("save1")->name == "My Save", "renamed, trimmed");
    expect(!store.rename("save1", "   ") && store.find("save1")->name == "My Save", "never to nothing");
    expect(store.take_next_start().empty() && !store.has_next_start(), "no save waits for the next start");
    expect(store.set_next_start("save1") && store.has_next_start(), "a save waits for the next start");
    expect(store.set_next_start("save1") && store.take_next_start() == "save1" && store.take_next_start().empty(),
           "the next start's save, taken once");

    SaveStore empty(scratch / "empty_saves");
    expect(empty.convert(scratch / "missing", 0u, {}) && empty.converted() && empty.list().empty(),
           "no old files: converted, with no saves");
}

void test_menu(const fs::path& scratch) {
    using galaxy::coop::MainMenu;
    using galaxy::coop::MenuCommand;

    MainMenu menu(true);
    expect(!menu.view().open, "closed until opened");
    menu.press(0u);
    expect(menu.take_command() == MenuCommand::None, "a closed menu takes no press");

    menu.open();
    galaxy::coop::MenuView view = menu.view();
    expect(view.open && view.page == 0u && view.pages.size() == 1u && view.pages[0] == "PLAY", "one page, open");
    expect(view.rows.size() == 4u && view.selected == 0u, "four rows, Host selected");
    expect(view.rows[0].label == "HOST" && view.rows[1].value == "OPEN" && view.rows[2].label == "OPTIONS" &&
               view.rows[2].value == "OPEN",
           "Host and Join open their pages, Options opens");

    menu.move(2);
    menu.press(0u);
    expect(menu.take_command() == MenuCommand::Settings, "Options opens the settings");
    expect(menu.take_command() == MenuCommand::None, "a command is taken once");
    menu.move(1);
    menu.press(0u);
    expect(menu.take_command() == MenuCommand::Quit, "Quit quits");
    menu.move(1);
    expect(menu.view().selected == 0u, "down from the last row wraps to the first");
    menu.move(-1);
    expect(menu.view().selected == 3u, "up from the first row wraps to the last");
    menu.select_row(9u);
    expect(menu.view().selected == 3u, "a row past the page is ignored");
    menu.press(1u);
    expect(menu.take_command() == MenuCommand::None, "a button the row doesn't have is ignored");
    menu.back();
    expect(menu.view().pages[0] == "PLAY", "B on the main page stays");

    MainMenu no_settings(false);
    no_settings.open();
    expect(no_settings.view().rows[2].value == "-OPEN", "Options unavailable without the in-game settings");
    no_settings.select_row(2u);
    no_settings.press(0u);
    expect(no_settings.take_command() == MenuCommand::None, "an unavailable Options does nothing");

    // The Host page, over two saves.
    galaxy::coop::SaveStore store(scratch / "menu_saves");
    const std::string first = store.create("ONE");
    const std::string second = store.create("TWO");
    store.mark_played(first, 50);
    MainMenu host(true, &store);
    host.open();
    host.press(0u);
    expect(host.take_command() == MenuCommand::None, "Host opens its page, nothing to play yet");
    view = host.view();
    expect(view.pages[0] == "HOST" && view.rows.size() == 12u && view.selected == 0u, "the Host page, Save selected");
    expect(view.rows[0].value == "ONE" && view.rows[0].changes && view.rows[0].name, "the last played save first");
    host.change(1);
    expect(host.view().rows[0].value == "TWO", "right: the next save");
    host.change(1);
    expect(host.view().rows[0].value == "NEW SAVE" && !host.view().rows[0].name &&
               host.view().rows[1].value == "-RENAME|-ERASE",
           "then a new one, which can't be renamed or erased");
    host.change(1);
    expect(host.view().rows[0].value == "NEW SAVE", "the new save is the last");
    host.change(2);
    expect(host.view().rows[0].value == "ONE", "a click on the row goes round to the first");
    host.change(-2);
    expect(host.view().rows[0].value == "NEW SAVE", "and back round to the last");
    host.change(-1);

    host.move(1);
    host.change(1);
    host.press(1u);
    expect(host.view().rows[1].value == "RENAME|PRESS AGAIN TO ERASE" && store.find(second), "Erase asks first");
    host.press(1u);
    expect(!store.find(second) && host.view().rows[0].value == "NEW SAVE", "then erases: the new save is next");
    host.move(-1);
    host.change(-1);
    expect(host.view().rows[0].value == "ONE", "left: back to the save");

    // Rename: the save's row takes the text, typed or letter by letter.
    host.select_row(1u);
    host.press(0u);
    view = host.view();
    expect(view.editing && view.selected == 0u && view.rows[0].value == "ONE_", "Rename edits the save's name");
    host.select_row(5u);
    expect(host.view().selected == 0u, "the row being edited keeps the selection");
    host.type(galaxy::coop::kTextBackspace);
    host.type(U'!');
    host.type(galaxy::coop::kTextLeft);
    expect(host.view().rows[0].value == "ON[!]", "typed at the cursor, which the brackets show");
    host.move(-1);
    expect(host.view().rows[0].value == "ON[?]", "up: the next letter at the cursor");
    host.reset();
    expect(host.view().rows[0].value == "ON_", "Tab or Minus deletes it");
    host.change(1);
    host.move(-1);
    expect(host.view().rows[0].value == "ON[A]", "up past the end adds a letter, the cursor on it");
    host.press(0u);
    expect(!host.view().editing && host.view().rows[0].value == "ONA" && store.find(first)->name == "ONA",
           "A keeps it: the save is renamed");
    host.select_row(1u);
    host.press(0u);
    for (int i = 0; i < 3; ++i) host.type(galaxy::coop::kTextBackspace);
    host.press(0u);
    expect(host.view().editing && host.view().rows[0].help == "A SAVE NEEDS A NAME", "an empty name isn't kept");
    host.back();
    expect(!host.view().editing && store.find(first)->name == "ONA", "B drops the edit");

    // The port and password.
    host.select_row(2u);
    host.press(0u);
    host.type(U'x');
    host.type(U'1');
    expect(host.view().rows[2].value == "77771_", "a port takes digits");
    host.type(galaxy::coop::kTextEnter);
    expect(host.view().editing && host.room().port == 7777u, "a port past 65535 isn't kept");
    host.type(galaxy::coop::kTextBackspace);
    host.type(galaxy::coop::kTextBackspace);
    host.type(U'8');
    host.type(galaxy::coop::kTextEnter);
    expect(!host.view().editing && host.room().port == 7778u, "Enter keeps the port");
    host.reset();
    expect(host.room().port == galaxy::kCoopDefaultPort, "Tab puts it back to 7777");
    host.select_row(3u);
    expect(host.view().rows[3].value == "NONE", "no password");
    host.press(0u);
    host.type(U'p');
    host.type(U'w');
    host.type(galaxy::coop::kTextEscape);
    expect(host.room().password.empty(), "Escape drops it");
    host.press(0u);
    host.type(U'p');
    host.type(U'w');
    host.press(0u);
    expect(host.room().password == "pw" && host.view().rows[3].value == "**", "kept, and hidden");

    const galaxy::RuntimeCoopSettings before = host.room();
    host.select_row(4u);
    host.change(1);
    host.select_row(8u);
    host.change(1);
    const galaxy::RuntimeCoopSettings after = host.room();
    expect(after.max_players == before.max_players + 1u && after.cheats, "the room's settings change");
    host.select_row(6u);
    host.change(1);
    host.change(1);
    expect(host.room().knockback_strength == galaxy::kCoopKnockbackTooMuch,
           "knockback stops at too much");

    expect(host.view().back && !MainMenu(true).view().back, "the Host page has a way back, the main page none");
    host.reset();
    expect(host.room().knockback_strength == galaxy::kCoopKnockbackNormal, "Tab resets the selected setting");
    expect(host.room().cheats, "and only it");
    host.select_row(8u);
    host.reset();
    expect(!host.room().cheats, "cheats reset to off");
    host.select_row(0u);
    host.reset();
    expect(host.view().rows[0].value == "ONA", "the save has no default");

    host.select_row(10u);
    host.press(0u);
    expect(host.take_command() == MenuCommand::Play && host.chosen_save() == first, "Host plays the picked save");

    host.select_row(11u);
    host.press(0u);
    expect(host.view().pages[0] == "PLAY" && host.view().selected == 0u, "Back returns to the main page, on Host");
    host.press(0u);
    host.select_row(0u);
    host.change(1);
    host.select_row(10u);
    host.press(0u);
    expect(host.take_command() == MenuCommand::Play && host.chosen_save().empty(), "Host on New save: a new one");
    host.back();
    expect(host.view().pages[0] == "PLAY", "B returns to the main page");

    host.close();
    expect(!host.view().open, "closed");
    host.open();
    expect(host.view().selected == 0u && host.view().pages[0] == "PLAY", "reopening starts at Host");
    host.play(second);
    expect(host.take_command() == MenuCommand::Play && host.chosen_save() == second, "a save played at once");
}

void test_join() {
    using galaxy::coop::MainMenu;
    using galaxy::coop::MenuCommand;
    MainMenu menu(true);
    menu.open();
    menu.select_row(1u);
    menu.press(0u);
    galaxy::coop::MenuView view = menu.view();
    expect(view.pages[0] == "JOIN" && view.rows.size() == 4u && view.rows[0].value == "NONE" &&
               view.rows[2].value == "-JOIN",
           "the Join page: no address yet, so no joining");
    menu.press(0u);
    for (const char c : std::string("bad address")) menu.type(static_cast<char32_t>(c));
    expect(menu.view().rows[0].value == "badaddress_", "an address takes no spaces");
    for (int i = 0; i < 10; ++i) menu.type(galaxy::coop::kTextBackspace);
    for (const char c : std::string("[::1")) menu.type(static_cast<char32_t>(c));
    menu.press(0u);
    expect(menu.view().editing && menu.room().join_address.empty(), "a malformed address isn't kept");
    for (const char c : std::string("]:7778")) menu.type(static_cast<char32_t>(c));
    menu.press(0u);
    expect(!menu.view().editing && menu.room().join_address == "[::1]:7778", "an address is kept");
    menu.select_row(2u);
    expect(menu.view().rows[2].value == "JOIN", "then Join can be pressed");
    menu.press(0u);
    expect(menu.take_command() == MenuCommand::Join, "and joins");
    menu.set_joining(true);
    view = menu.view();
    expect(view.pages[0] == "JOINING" && view.rows.size() == 3u && view.rows[0].value == "[::1]:7778" &&
               view.rows[1].bar_steps == 100u && view.rows[1].bar_at == 0u && view.rows[2].value == "CANCEL" &&
               view.selected == 2u,
           "while connecting, the joining screen: the address, an empty loading bar and Cancel");
    menu.move(-1);
    menu.select_row(0u);
    expect(menu.view().selected == 2u, "which keeps the selection on Cancel");
    menu.set_join_status("DOWNLOADING THE ROOM'S MODS: 50%", "SomeMod", 60u);
    view = menu.view();
    expect(view.rows[0].label == "DOWNLOADING THE ROOM'S MODS: 50%" && view.rows[1].label == "SomeMod" &&
               view.rows[1].label_name && view.rows[1].bar_at == 60u && view.rows[1].value == "60%",
           "the join's step and how far it is");
    menu.press(0u);
    expect(menu.take_command() == MenuCommand::CancelJoin, "Cancel stops the join");
    menu.back();
    expect(menu.take_command() == MenuCommand::CancelJoin, "as B does");
    menu.set_joining(false);
    view = menu.view();
    expect(view.pages[0] == "JOIN" && view.selected == 2u && view.rows[2].value == "JOIN",
           "stopped, the Join page again");
    menu.set_joining(true);
    menu.finish_join("JOINED!");
    view = menu.view();
    expect(!menu.joining() && view.pages[0] == "JOINING" && view.rows[0].label == "JOINED!" &&
               view.rows[1].bar_at == 100u && view.rows[2].value == "-CANCEL",
           "joined: the joining screen stays full, with nothing to cancel");
    menu.press(0u);
    menu.back();
    expect(menu.take_command() == MenuCommand::None, "and Cancel and B do nothing");
    menu.set_joining(false);
    expect(menu.view().pages[0] == "JOINING", "until the menu closes");
    menu.close();
    menu.open();
    menu.select_row(1u);
    menu.press(0u);
    menu.back();
    expect(menu.view().pages[0] == "PLAY" && menu.view().selected == 1u, "back on Join");

}

// A text row's editing, as Options' PLAYER page and the main menu's text rows have it.
void test_text_edit() {
    using galaxy::coop::TextEdit;
    using galaxy::coop::TextKind;
    TextEdit name(TextKind::PlayerName, {});
    expect(name.shown() == "_", "an empty text: the cursor where the first letter goes");
    // Letter by letter, as a controller: up from nothing starts at A, down at the last letter.
    name.step_letter(1);
    name.step_letter(1);
    expect(name.type(galaxy::coop::kTextRight) == TextEdit::Result::Editing, "right moves on");
    name.step_letter(-1);
    expect(name.shown() == "B[']", "letters picked by a pad: down past the end starts at the last");
    name.erase();
    expect(name.text() == "B" && name.shown() == "B_", "Minus deletes the letter at the cursor");
    expect(name.type(galaxy::coop::kTextEnter) == TextEdit::Result::Keep && name.text() == "B", "Enter keeps it");
    for (int i = 0; i < 30; ++i) name.type(U'x');
    expect(name.text().size() == galaxy::coop::kPlayerNameLimit, "a name is cut at its limit");
    name.type(galaxy::coop::kTextHome);
    name.type(galaxy::coop::kTextDelete);
    expect(name.text().size() == galaxy::coop::kPlayerNameLimit - 1u && name.shown().rfind("[x]", 0) == 0u,
           "Home, then Delete takes the first letter");
    expect(name.type(galaxy::coop::kTextEscape) == TextEdit::Result::Drop, "Escape drops it");

    TextEdit port(TextKind::Port, "77");
    port.type(U'a');
    port.type(U'7');
    for (int i = 0; i < 5; ++i) port.type(U'1');
    expect(port.text() == "77711", "a port takes five digits only");
}

void test_mods() {
    using galaxy::coop::MainMenu;
    using galaxy::coop::RoomMod;
    MainMenu menu(true);
    menu.set_mods_source([] {
        return std::vector<RoomMod>{{"Alpha", false}, {"Textures", true}, {"Zed", false}, {"Music", true}};
    });
    menu.open();
    menu.press(0u);
    expect(menu.view().rows[9].value == "2 ON" && !menu.mods_changed(), "the Host page counts the mods on");
    menu.select_row(9u);
    menu.press(0u);
    galaxy::coop::MenuView view = menu.view();
    expect(view.pages[0] == "MODS" && view.rows.size() == 5u && view.rows[0].label == "Textures" &&
               view.rows[0].value == "ON (1)" && view.rows[1].value == "ON (2)" && view.rows[2].value == "OFF" &&
               view.rows[0].label_name,
           "the mods page: the ones on first, numbered");
    menu.select_row(3u);
    menu.press(0u);
    view = menu.view();
    expect(view.rows[0].label == "Zed" && view.rows[0].value == "ON (1)" && view.selected == 0u && menu.mods_changed(),
           "turned on: on top, winning");
    menu.change(1);
    view = menu.view();
    expect(view.rows[3].label == "Zed" && view.rows[3].value == "OFF" && view.selected == 3u && !menu.mods_changed(),
           "turned off again: among the others by name");
    menu.select_row(1u);
    menu.reset();
    expect(menu.mods_changed() && menu.room_mods()[1].id == "Alpha" && !menu.room_mods()[1].on,
           "Tab turns a mod off");
    menu.back();
    expect(menu.view().pages[0] == "HOST" && menu.view().selected == 9u && menu.view().rows[9].value == "1 ON",
           "B: back to the Host page, its mods kept");
    menu.press(0u);
    menu.select_row(4u);
    menu.press(0u);
    expect(menu.view().pages[0] == "HOST" && menu.mods_changed(), "Done: back too");

    MainMenu none(true);
    none.open();
    none.press(0u);
    none.select_row(9u);
    none.press(0u);
    expect(none.view().rows.size() == 2u && none.view().rows[0].label == "NO MODS YET", "no mods to pick");
}
}  // namespace

int main() {
    std::error_code error;
    const fs::path scratch = fs::temp_directory_path(error) / "galaxy_coop_menu_tests";
    fs::remove_all(scratch, error);
    fs::create_directories(scratch, error);
    test_saves(scratch);
    test_menu(scratch);
    test_join();
    test_text_edit();
    test_mods();
    fs::remove_all(scratch, error);

    if (s_failures != 0) return 1;
    std::puts("Co-op menu tests passed");
    return 0;
}
