#include "galaxy/coop_menu.h"

#include "galaxy/coop_saves.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <string_view>
#include <utility>

namespace galaxy::coop {

namespace {

enum MainRow : unsigned { RowHost, RowJoin, RowOptions, RowQuit, MainRowCount };
enum HostRow : unsigned {
    RowSave,
    RowSaveEdit,
    RowPort,
    RowPassword,
    RowPlayers,
    RowInteraction,
    RowKnockback,
    RowPvp,
    RowCheats,
    RowMods,
    RowStart,
    RowBack,
    HostRowCount
};
enum JoinRow : unsigned { RowAddress, RowJoinPassword, RowJoinStart, RowJoinBack, JoinRowCount };
enum JoiningRow : unsigned { RowJoinStep, RowJoinBar, RowJoinCancel, JoiningRowCount };

constexpr unsigned kKnockbacks[] = {kCoopKnockbackWeak, kCoopKnockbackNormal, kCoopKnockbackTooMuch};

// The letters up and down go through on a controller, by what the row holds (typing takes any of them too).
constexpr std::string_view kNameLetters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_.!?'";
constexpr std::string_view kPasswordLetters =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.!?#$%&*+=@";
constexpr std::string_view kAddressLetters = "0123456789.:abcdefghijklmnopqrstuvwxyz-[]";
constexpr std::string_view kPortLetters = "0123456789";

std::string_view kind_letters(TextKind kind) {
    switch (kind) {
    case TextKind::Port: return kPortLetters;
    case TextKind::Password: return kPasswordLetters;
    case TextKind::Address: return kAddressLetters;
    default: return kNameLetters;
    }
}

std::size_t kind_limit(TextKind kind) {
    switch (kind) {
    case TextKind::Port: return 5u;
    case TextKind::Address: return kCoopAddressLimit;
    case TextKind::PlayerName: return kPlayerNameLimit;
    default: return kCoopTextLimit;
    }
}

constexpr const char* kEditHelp =
    "TYPE IT, OR UP AND DOWN PICK A LETTER AND LEFT AND RIGHT MOVE. A OR ENTER KEEPS IT, B OR ESCAPE DROPS IT";

// The choice nearest `strength` (an edited file's in between rounds to one).
unsigned knockback_index(unsigned strength) {
    unsigned best = 0u;
    for (unsigned i = 1u; i < std::size(kKnockbacks); ++i) {
        const auto distance = [strength](unsigned value) {
            return value > strength ? value - strength : strength - value;
        };
        if (distance(kKnockbacks[i]) < distance(kKnockbacks[best])) best = i;
    }
    return best;
}

const char* interaction_word(CoopPlayerInteraction interaction) {
    switch (interaction) {
    case CoopPlayerInteraction::NonSolid: return "NON-SOLID";
    case CoopPlayerInteraction::Solid: return "SOLID";
    case CoopPlayerInteraction::FriendlyFire: return "FRIENDLY FIRE";
    }
    return "SOLID";
}

// `value` stepped by `delta` within [0, count), stopping at the ends.
// `delta` -2 or 2 (a click on the row) goes round past the ends instead.
unsigned step(unsigned value, int delta, unsigned count) {
    const int next = static_cast<int>(value) + (delta < 0 ? -1 : 1);
    const int last = static_cast<int>(count) - 1;
    if (delta <= -2 || delta >= 2) return static_cast<unsigned>(next < 0 ? last : next > last ? 0 : next);
    return static_cast<unsigned>(std::clamp(next, 0, last));
}

// `text` without the spaces around it.
std::string trimmed(std::string_view text) {
    const std::size_t begin = text.find_first_not_of(' ');
    if (begin == std::string_view::npos) return {};
    return std::string(text.substr(begin, text.find_last_not_of(' ') - begin + 1u));
}

// A port, 1 to 65535, in decimal digits only.
bool parse_port(std::string_view text, std::uint16_t& port) {
    if (text.empty() || text.size() > 5u ||
        !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    const unsigned long value = std::strtoul(std::string(text).c_str(), nullptr, 10);
    if (value < 1u || value > 65535u) return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

// The mods on, counted.
unsigned mods_on(const std::vector<RoomMod>& mods) {
    return static_cast<unsigned>(std::count_if(mods.begin(), mods.end(), [](const RoomMod& mod) { return mod.on; }));
}

}  // namespace

TextEdit::TextEdit(TextKind kind, std::string text) : kind_(kind), text_(std::move(text)), cursor_(text_.size()) {}

std::string TextEdit::shown() const {
    if (cursor_ >= text_.size()) return text_ + "_";
    return text_.substr(0u, cursor_) + "[" + text_[cursor_] + "]" + text_.substr(cursor_ + 1u);
}

void TextEdit::step_letter(int delta) {
    const std::string_view letters = kind_letters(kind_);
    if (cursor_ >= text_.size()) {
        // Past the end: a new letter, the first (up) or the last (down).
        if (text_.size() >= kind_limit(kind_)) return;
        text_.push_back(delta > 0 ? letters.front() : letters.back());
        return;
    }
    const std::size_t at = letters.find(text_[cursor_]);
    const std::size_t count = letters.size();
    const std::size_t from = at == std::string_view::npos ? (delta > 0 ? count - 1u : 0u) : at;
    text_[cursor_] = letters[(from + (delta > 0 ? 1u : count - 1u)) % count];
}

void TextEdit::erase() {
    (void)type(cursor_ < text_.size() ? kTextDelete : kTextBackspace);
}

TextEdit::Result TextEdit::type(char32_t key) {
    switch (key) {
    case kTextEnter: return Result::Keep;
    case kTextEscape: return Result::Drop;
    case kTextBackspace:
        if (cursor_ > 0u) text_.erase(--cursor_, 1u);
        return Result::Editing;
    case kTextDelete:
        if (cursor_ < text_.size()) text_.erase(cursor_, 1u);
        return Result::Editing;
    case kTextLeft:
        if (cursor_ > 0u) --cursor_;
        return Result::Editing;
    case kTextRight:
        // Past the last letter onto the place a new one goes.
        if (cursor_ < text_.size()) ++cursor_;
        return Result::Editing;
    case kTextUp: step_letter(1); return Result::Editing;
    case kTextDown: step_letter(-1); return Result::Editing;
    case kTextHome: cursor_ = 0u; return Result::Editing;
    case kTextEnd: cursor_ = text_.size(); return Result::Editing;
    default: break;
    }
    if (key < 0x20 || key >= 0x7F) return Result::Editing;  // the game's font draws printable ASCII
    const char letter = static_cast<char>(key);
    bool allowed = true;
    if (kind_ == TextKind::Port) allowed = letter >= '0' && letter <= '9';
    if (kind_ == TextKind::Address) allowed = letter != ' ' && letter != '/';
    if (allowed && text_.size() < kind_limit(kind_)) text_.insert(cursor_++, 1u, letter);
    return Result::Editing;
}

MainMenu::MainMenu(bool settings, SaveStore* saves) : settings_(settings), saves_(saves) {}

void MainMenu::open() {
    if (open_) return;
    open_ = true;
    command_ = MenuCommand::None;
    join_done_ = false;
    show(Screen::Main);
}

void MainMenu::set_joining(bool joining) {
    joining_ = joining;
    if (joining) {
        join_step_ = "CONNECTING TO THE HOST...";
        join_detail_.clear();
        join_percent_ = 0u;
        join_done_ = false;
        show(Screen::Joining, RowJoinCancel);
    } else if (screen_ == Screen::Joining && !join_done_) {
        show(Screen::Join, RowJoinStart);
    }
}

void MainMenu::set_join_status(std::string step, std::string detail, unsigned percent) {
    join_step_ = std::move(step);
    join_detail_ = std::move(detail);
    join_percent_ = std::min(percent, 100u);
}

void MainMenu::finish_join(std::string step) {
    joining_ = false;
    join_done_ = true;
    set_join_status(std::move(step), {}, 100u);
}

void MainMenu::close() {
    open_ = false;
    edit_ = {};
}

void MainMenu::show(Screen screen, unsigned row) {
    const Screen from = screen_;
    screen_ = screen;
    selected_ = row;
    selected_button_ = 0u;
    erase_armed_ = false;
    edit_ = {};
    message_.clear();
    // The Host page: the saves and mods as it opens (not again back from the mods), the last played save first: it
    // is the one picked.
    if (screen != Screen::Host || from == Screen::Mods) return;
    save_ids_.clear();
    save_names_.clear();
    if (saves_ != nullptr) {
        for (const SaveInfo& save : saves_->list()) {
            save_ids_.push_back(save.id);
            save_names_.push_back(save.name);
        }
    }
    save_ = 0u;
    read_mods();
}

unsigned MainMenu::star_counter_row() const {
    return static_cast<unsigned>(std::max<std::size_t>(mods_.size(), 1u));
}

void MainMenu::read_mods() {
    mods_read_ = mods_source_ ? mods_source_() : std::vector<RoomMod>{};
    // The ones on first, in their order; the others after them.
    std::stable_partition(mods_read_.begin(), mods_read_.end(), [](const RoomMod& mod) { return mod.on; });
    mods_ = mods_read_;
}

unsigned MainMenu::row_count() const {
    switch (screen_) {
    case Screen::Main: return MainRowCount;
    case Screen::Host: return HostRowCount;
    case Screen::Mods: return star_counter_row() + 2u;
    case Screen::Join: return JoinRowCount;
    case Screen::Joining: return JoiningRowCount;
    }
    return 0u;
}

std::string MainMenu::text_value(Field field, unsigned row, std::string value) const {
    if (edit_.field != field || edit_.row != row) return value;
    return edit_.text.shown();
}

std::vector<MenuRow> MainMenu::rows() const {
    switch (screen_) {
    case Screen::Main: {
        std::vector<MenuRow> rows(MainRowCount);
        rows[RowHost] = {"HOST", "OPEN", "PICK A SAVE AND THE ROOM'S SETTINGS, THEN PLAY", true};
        rows[RowJoin] = {"JOIN", "OPEN", "THE ADDRESS OF ANOTHER PLAYER'S GAME", true};
        rows[RowOptions] = settings_
            ? MenuRow{"OPTIONS", "OPEN", "YOUR NAME AND CHARACTER, AND NEBULA'S SETTINGS: DISPLAY, GRAPHICS, AUDIO",
                      true}
            : MenuRow{"OPTIONS", "-OPEN", "NEBULA'S IN-GAME SETTINGS AREN'T ON THIS SYSTEM YET", true};
        rows[RowQuit] = {"QUIT", "QUIT TO DESKTOP", "CLOSES NEBULA", true};
        return rows;
    }
    case Screen::Host: {
        const RuntimeCoopSettings& coop = room_;
        const bool fresh = save_ >= save_ids_.size();
        std::vector<MenuRow> rows(HostRowCount);
        const bool renaming = edit_.field == Field::SaveName;
        rows[RowSave] = {"SAVE",
                         renaming ? text_value(Field::SaveName, RowSave, {})
                         : fresh  ? "NEW SAVE"
                                  : save_names_[save_],
                         "LEFT AND RIGHT PICK A SAVE, OR A NEW ONE. YOUR SAVES ARE YOURS: PLAYERS WHO JOIN PLAY IN THEM",
                         false, !renaming, renaming || !fresh};
        if (renaming) rows[RowSave].help = kEditHelp;
        rows[RowSaveEdit] = {"",
                             std::string(fresh ? "-RENAME|-ERASE" : "RENAME|") +
                                 (fresh ? "" : erase_armed_ ? "PRESS AGAIN TO ERASE" : "ERASE"),
                             "RENAME: A NEW NAME FOR THE SAVE. ERASE: THE SAVE AND ITS ACHIEVEMENTS, FOR GOOD", true};
        rows[RowPort] = {"PORT", text_value(Field::Port, RowPort, std::to_string(coop.port)),
                         "THE UDP PORT PLAYERS JOIN ON. FORWARD IT ON YOUR ROUTER, OR PLAY OVER A VPN", false, false,
                         true, true};
        rows[RowPassword] = {"PASSWORD",
                             edit_.field == Field::Password && edit_.row == RowPassword
                                 ? text_value(Field::Password, RowPassword, {})
                             : coop.password.empty() ? "NONE"
                                                     : std::string(coop.password.size(), '*'),
                             "PLAYERS WHO JOIN MUST GIVE IT. NONE: ANYONE WITH THE ADDRESS MAY JOIN", false, false,
                             !coop.password.empty() || edit_.field == Field::Password, true};
        rows[RowPlayers] = {"MAX PLAYERS", std::to_string(coop.max_players),
                            "HOW MANY PLAYERS THE ROOM HOLDS, YOU INCLUDED", false, true};
        rows[RowInteraction] = {"PLAYER INTERACTION", interaction_word(coop.player_interaction),
                                "NON-SOLID: PLAYERS PASS THROUGH. SOLID: THEY BUMP AND KNOCK BACK. FRIENDLY FIRE: "
                                "ATTACKS HURT",
                                false, true};
        static const char* const knockbacks[] = {"WEAK", "NORMAL", "TOO MUCH"};
        rows[RowKnockback] = {"KNOCKBACK STRENGTH", knockbacks[knockback_index(coop.knockback_strength)],
                              "HOW FAR A PLAYER'S ATTACK KNOCKS ANOTHER", false, true};
        rows[RowPvp] = {"PVP MODE", coop.pvp_mode == CoopPvpMode::Revamped ? "REVAMPED" : "CLASSIC",
                        "FRIENDLY FIRE'S RULES: CLASSIC, OR REVAMPED (WHICH ATTACKS BEAT WHICH)", false, true};
        rows[RowCheats] = {"CHEATS", coop.cheats ? "ON" : "OFF",
                           "ON: EVERYONE IN THE ROOM MAY USE THE ONLINE TAB'S CHEATS", false, true};
        const unsigned on = mods_on(mods_) + (coop.star_counter ? 1u : 0u);
        rows[RowMods] = {"MODS", on == 0u ? "NONE" : std::to_string(on) + " ON",
                         "THE MODS THE ROOM PLAYS WITH. PLAYERS WHO JOIN GET THEM FROM YOU", true};
        rows[RowStart] = {"HOST", "HOST", "PLAYS THE SAVE AND OPENS THE ROOM: OTHERS JOIN YOUR ADDRESS AND PORT", true};
        rows[RowBack] = {"BACK", "BACK", "BACK TO THE MAIN MENU", true};
        if (edit_.field == Field::Port || edit_.field == Field::Password) rows[edit_.row].help = kEditHelp;
        return rows;
    }
    case Screen::Mods: {
        std::vector<MenuRow> rows;
        unsigned place = 0u;
        for (const RoomMod& mod : mods_) {
            MenuRow row{mod.id, mod.on ? "ON (" + std::to_string(++place) + ")" : "OFF",
                        "ON FOR THE ROOM: THE NUMBER IS ITS PLACE, 1 WINNING WHERE TWO MODS CHANGE THE SAME THING",
                        false, true};
            row.label_name = true;
            rows.push_back(std::move(row));
        }
        if (mods_.empty()) {
            rows.push_back({"NO MODS YET", {}, "IMPORT MODS IN OPTIONS, ON THE MODS PAGE"});
        }
        // Nebula's own mods, after the folder's: nothing to download, and they apply without a restart.
        rows.push_back({"PERSONAL STAR COUNTER", room_.star_counter ? "ON" : "OFF",
                        "BUILT IN: EACH PLAYER'S STARS IN THIS ROOM (RED) AND EVER (GREEN) UNDER THE STAR COUNT, AND "
                        "IN THE PLAYER LIST",
                        false, true});
        rows.push_back({"", "DONE", "BACK TO THE HOST PAGE. NEW MODS APPLY WHEN NEBULA STARTS AGAIN, WHICH HOST DOES",
                        true});
        return rows;
    }
    case Screen::Join: {
        const RuntimeCoopSettings& coop = room_;
        std::vector<MenuRow> rows(JoinRowCount);
        rows[RowAddress] = {"ADDRESS",
                            edit_.field == Field::Address ? text_value(Field::Address, RowAddress, {})
                            : coop.join_address.empty()   ? "NONE"
                                                          : coop.join_address,
                            "THE HOST'S ADDRESS: HOST, HOST:PORT OR [IPV6]:PORT. A HOST WITHOUT A PORT IS ON 7777",
                            false, false, !coop.join_address.empty() || edit_.field == Field::Address, true};
        rows[RowJoinPassword] = {"PASSWORD",
                                 edit_.field == Field::Password ? text_value(Field::Password, RowJoinPassword, {})
                                 : coop.password.empty()        ? "NONE"
                                                                : std::string(coop.password.size(), '*'),
                                 "THE ROOM'S PASSWORD, WHEN IT HAS ONE", false, false,
                                 !coop.password.empty() || edit_.field == Field::Password, true};
        rows[RowJoinStart] = joining_                  ? MenuRow{"JOIN", "CANCEL", "CONNECTING TO THE HOST...", true}
                             : coop.join_address.empty() ? MenuRow{"JOIN", "-JOIN", "TYPE THE HOST'S ADDRESS FIRST", true}
                                                         : MenuRow{"JOIN", "JOIN",
                                                                   "CONNECTS TO THE HOST AND PLAYS IN THEIR ROOM", true};
        rows[RowJoinBack] = {"BACK", "BACK", "BACK TO THE MAIN MENU", true};
        if (editing()) rows[edit_.row].help = kEditHelp;
        return rows;
    }
    case Screen::Joining: {
        std::vector<MenuRow> rows(JoiningRowCount);
        rows[RowJoinStep] = {join_step_, room_.join_address, {}, false, false, true};
        rows[RowJoinBar] = {join_detail_, std::to_string(join_percent_) + "%", {}};
        rows[RowJoinBar].label_name = !join_detail_.empty();
        rows[RowJoinBar].bar_steps = 100u;
        rows[RowJoinBar].bar_at = join_percent_;
        rows[RowJoinCancel] = join_done_ ? MenuRow{"", "-CANCEL", "", true}
                                         : MenuRow{"", "CANCEL", "STOPS JOINING, BACK TO THE JOIN PAGE", true};
        return rows;
    }
    }
    return {};
}

unsigned MainMenu::button_count(unsigned row) const {
    const std::vector<MenuRow> all = rows();
    if (row >= all.size() || !all[row].buttons) return 0u;
    return static_cast<unsigned>(std::count(all[row].value.begin(), all[row].value.end(), '|')) + 1u;
}

MenuView MainMenu::view() const {
    static const char* const titles[] = {"PLAY", "HOST", "MODS", "JOIN", "JOINING"};
    MenuView view;
    view.open = open_;
    view.page = 0u;
    view.pages = {titles[static_cast<unsigned>(screen_)]};
    view.rows = rows();
    view.selected = selected_;
    view.selected_button = selected_button_;
    view.back = screen_ != Screen::Main;
    view.editing = editing();
    if (!message_.empty() && selected_ < view.rows.size()) view.rows[selected_].help = message_;
    return view;
}

void MainMenu::select_page(unsigned) {
    // One page a screen: their buttons move between them.
}

void MainMenu::select_row(unsigned row) {
    // The row being edited keeps the selection until its text is kept or dropped; the joining screen keeps it on
    // Cancel.
    if (editing() || row >= row_count() || row == selected_ || screen_ == Screen::Joining) return;
    selected_ = row;
    selected_button_ = 0u;
    erase_armed_ = false;
    message_.clear();
}

void MainMenu::move(int delta) {
    if (delta == 0) return;
    if (editing()) {
        // Up the next letter, down the one before.
        edit_.text.step_letter(delta < 0 ? 1 : -1);
        return;
    }
    const int count = static_cast<int>(row_count());
    if (count <= 0 || screen_ == Screen::Joining) return;
    const int next = ((static_cast<int>(selected_) + delta) % count + count) % count;
    select_row(static_cast<unsigned>(next));
}

void MainMenu::change(int delta) {
    if (delta == 0) return;
    message_.clear();
    if (editing()) {
        type(delta < 0 ? kTextLeft : kTextRight);
        return;
    }
    if (screen_ == Screen::Host) {
        RuntimeCoopSettings& coop = room_;
        switch (selected_) {
        case RowSave:
            save_ = step(save_, delta, static_cast<unsigned>(save_ids_.size()) + 1u);
            erase_armed_ = false;
            return;
        case RowPlayers:
            coop.max_players = step(coop.max_players - 2u, delta, kCoopMaxPlayersLimit - 1u) + 2u;
            break;
        case RowInteraction:
            coop.player_interaction = static_cast<CoopPlayerInteraction>(
                step(static_cast<unsigned>(coop.player_interaction), delta, 3u));
            break;
        case RowKnockback:
            coop.knockback_strength = kKnockbacks[step(knockback_index(coop.knockback_strength), delta, 3u)];
            break;
        case RowPvp:
            coop.pvp_mode = static_cast<CoopPvpMode>(step(static_cast<unsigned>(coop.pvp_mode), delta, 2u));
            break;
        case RowCheats: coop.cheats = step(coop.cheats ? 1u : 0u, delta, 2u) == 1u; break;
        default: break;
        }
    } else if (screen_ == Screen::Mods && selected_ < mods_.size()) {
        // Off to on and back: left and right both turn it over, as a click does.
        toggle_mod(selected_);
        return;
    } else if (screen_ == Screen::Mods && selected_ == star_counter_row()) {
        room_.star_counter = !room_.star_counter;
        return;
    }
    const unsigned count = button_count(selected_);
    if (count < 2u) return;
    selected_button_ = step(selected_button_, delta, count);
    erase_armed_ = false;
}

void MainMenu::toggle_mod(unsigned index) {
    RoomMod mod = mods_[index];
    mods_.erase(mods_.begin() + index);
    const auto first_off = std::find_if(mods_.begin(), mods_.end(), [](const RoomMod& m) { return !m.on; });
    if (!mod.on) {
        // On, on top of the others: it wins over them.
        mod.on = true;
        mods_.insert(mods_.begin(), std::move(mod));
        selected_ = 0u;
        return;
    }
    // Off, among the others by name.
    mod.on = false;
    const auto at = std::find_if(first_off, mods_.end(), [&mod](const RoomMod& m) { return m.id > mod.id; });
    selected_ = static_cast<unsigned>(at - mods_.begin());
    mods_.insert(at, std::move(mod));
}

void MainMenu::press(unsigned button) {
    if (!open_) return;
    message_.clear();
    if (editing()) {
        finish_edit();
        return;
    }
    const std::vector<MenuRow> all = rows();
    if (selected_ >= all.size()) return;
    const MenuRow& row = all[selected_];
    if (row.text) {
        switch (screen_) {
        case Screen::Host: start_edit(selected_ == RowPort ? Field::Port : Field::Password, selected_); break;
        case Screen::Join: start_edit(selected_ == RowAddress ? Field::Address : Field::Password, selected_); break;
        default: break;
        }
        return;
    }
    if (screen_ == Screen::Mods && selected_ < mods_.size()) {
        toggle_mod(selected_);
        return;
    }
    if (screen_ == Screen::Mods && selected_ == star_counter_row()) {
        room_.star_counter = !room_.star_counter;
        return;
    }
    if (button >= button_count(selected_)) return;
    // An unavailable button ('-' before its word) does nothing.
    const std::string& value = row.value;
    std::size_t start = 0u;
    for (unsigned i = 0u; i < button; ++i) start = value.find('|', start) + 1u;
    if (value[start] == '-') return;
    switch (screen_) {
    case Screen::Main: press_main(selected_); return;
    case Screen::Host: press_host(selected_, button); return;
    case Screen::Mods: press_mods(selected_); return;
    case Screen::Join:
        if (selected_ == RowJoinStart) command_ = joining_ ? MenuCommand::CancelJoin : MenuCommand::Join;
        if (selected_ == RowJoinBack) back();
        return;
    case Screen::Joining:
        if (selected_ == RowJoinCancel && joining_) command_ = MenuCommand::CancelJoin;
        return;
    }
}

void MainMenu::press_main(unsigned row) {
    switch (row) {
    case RowHost: show(Screen::Host); break;
    case RowJoin: show(Screen::Join); break;
    case RowOptions: command_ = MenuCommand::Settings; break;
    case RowQuit: command_ = MenuCommand::Quit; break;
    default: break;
    }
}

void MainMenu::press_host(unsigned row, unsigned button) {
    switch (row) {
    case RowSaveEdit:
        if (button == 0u) {
            erase_armed_ = false;
            selected_ = RowSave;
            selected_button_ = 0u;
            start_edit(Field::SaveName, RowSave);
            return;
        }
        if (!erase_armed_) {
            erase_armed_ = true;
            return;
        }
        erase_armed_ = false;
        if (saves_ != nullptr && save_ < save_ids_.size() && saves_->erase(save_ids_[save_])) {
            save_ids_.erase(save_ids_.begin() + save_);
            save_names_.erase(save_names_.begin() + save_);
        }
        return;
    case RowMods: show(Screen::Mods); return;
    case RowStart:
        chosen_save_ = save_ < save_ids_.size() ? save_ids_[save_] : std::string();
        command_ = MenuCommand::Play;
        return;
    case RowBack: back(); return;
    default: return;
    }
}

void MainMenu::press_mods(unsigned row) {
    if (row + 1u == row_count()) show(Screen::Host, RowMods);
}

void MainMenu::start_edit(Field field, unsigned row) {
    edit_.field = field;
    edit_.row = row;
    switch (field) {
    case Field::SaveName:
        edit_.text = TextEdit(TextKind::Name, save_ < save_names_.size() ? save_names_[save_] : std::string());
        break;
    case Field::Port: edit_.text = TextEdit(TextKind::Port, std::to_string(room_.port)); break;
    case Field::Password: edit_.text = TextEdit(TextKind::Password, room_.password); break;
    case Field::Address: edit_.text = TextEdit(TextKind::Address, room_.join_address); break;
    case Field::None: break;
    }
}

// The text edited kept, when it makes sense for its row; else it stays open, saying why.
void MainMenu::finish_edit() {
    const std::string text = trimmed(edit_.text.text());
    switch (edit_.field) {
    case Field::SaveName:
        if (text.empty()) {
            message_ = "A SAVE NEEDS A NAME";
            return;
        }
        if (saves_ != nullptr && save_ < save_ids_.size() && saves_->rename(save_ids_[save_], text)) {
            save_names_[save_] = saves_->find(save_ids_[save_])->name;
        }
        break;
    case Field::Port: {
        std::uint16_t port = 0u;
        if (!parse_port(text, port)) {
            message_ = "A PORT IS A NUMBER FROM 1 TO 65535";
            return;
        }
        room_.port = port;
        break;
    }
    case Field::Password: room_.password = text; break;
    case Field::Address: {
        std::string host;
        std::uint16_t port = 0u;
        if (!text.empty() && !parse_coop_endpoint(text, host, port)) {
            message_ = "THAT ISN'T AN ADDRESS: HOST, HOST:PORT OR [IPV6]:PORT";
            return;
        }
        room_.join_address = text;
        break;
    }
    case Field::None: return;
    }
    edit_ = {};
}

bool MainMenu::type(char32_t key) {
    if (!editing()) return false;
    message_.clear();
    switch (edit_.text.type(key)) {
    case TextEdit::Result::Keep: finish_edit(); break;
    case TextEdit::Result::Drop: edit_ = {}; break;
    case TextEdit::Result::Editing: break;
    }
    return true;
}

void MainMenu::reset() {
    if (!open_) return;
    message_.clear();
    if (editing()) {
        edit_.text.erase();
        return;
    }
    const RuntimeCoopSettings defaults{};
    if (screen_ == Screen::Join) {
        if (selected_ == RowAddress) room_.join_address.clear();
        if (selected_ == RowJoinPassword) room_.password.clear();
        return;
    }
    if (screen_ == Screen::Mods) {
        if (selected_ < mods_.size() && mods_[selected_].on) {
            toggle_mod(selected_);
        } else if (selected_ == star_counter_row()) {
            room_.star_counter = defaults.star_counter;
        }
        return;
    }
    if (screen_ != Screen::Host) return;
    switch (selected_) {
    case RowPort: room_.port = defaults.port; break;
    case RowPassword: room_.password.clear(); break;
    case RowPlayers: room_.max_players = defaults.max_players; break;
    case RowInteraction: room_.player_interaction = defaults.player_interaction; break;
    case RowKnockback: room_.knockback_strength = defaults.knockback_strength; break;
    case RowPvp: room_.pvp_mode = defaults.pvp_mode; break;
    case RowCheats: room_.cheats = defaults.cheats; break;
    default: break;  // the save, the mods and the buttons have no default
    }
}

void MainMenu::back() {
    if (!open_) return;
    message_.clear();
    if (editing()) {
        edit_ = {};
        return;
    }
    switch (screen_) {
    case Screen::Main: return;
    case Screen::Host: show(Screen::Main, RowHost); return;
    case Screen::Mods: show(Screen::Host, RowMods); return;
    case Screen::Join:
        if (joining_) command_ = MenuCommand::CancelJoin;
        show(Screen::Main, RowJoin);
        return;
    case Screen::Joining:
        // The host stops the join and shows the Join page again (set_joining).
        if (joining_) command_ = MenuCommand::CancelJoin;
        return;
    }
}

MenuCommand MainMenu::take_command() noexcept {
    return std::exchange(command_, MenuCommand::None);
}

void MainMenu::play(std::string id) {
    chosen_save_ = std::move(id);
    command_ = MenuCommand::Play;
}

}  // namespace galaxy::coop
