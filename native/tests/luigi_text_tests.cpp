// NebulaLuigiText::nameMario: Luigi's name, face and color as Mario's in the game's text, other tags left whole.
#include "Game/System/NebulaLuigiText.hpp"

#include <cstdio>
#include <string>

namespace {
    int sFailures;

    void check(bool ok, const char* pWhat) {
        if (!ok) {
            printf("FAIL %s\n", pWhat);
            sFailures++;
        }
    }

    std::u16string named(const std::u16string& rSrc, s32 capacity = 128) {
        char16_t dst[256];
        const s32 length = NebulaLuigiText::nameMario(dst, capacity, rSrc.c_str(), u"Luigi", u"Mario");
        return length < 0 ? u"<none>" : std::u16string(dst, length);
    }

    // A picture tag (escape, head: group and 6 bytes, the picture's number).
    std::u16string picture(char16_t number) {
        std::u16string tag = u"\x1A";
        tag += makeMessageTagHead(MessageTagGroup_Picture, 6);
        tag += number;
        return tag;
    }

    // A text color tag (group 255, number 0, the color's index in the parameter's high byte).
    std::u16string color(u8 index) {
        std::u16string tag = u"\x1A";
        tag += makeMessageTagHead(MessageTagGroup_System, 8);
        tag += u'\0';
        tag += static_cast< char16_t >(index << 8);
        return tag;
    }
}  // namespace

int main() {
    check(named(u"we got lost with Luigi...") == u"we got lost with Mario...", "the name");
    check(named(u"Luigi & Luigi") == u"Mario & Mario", "each one");
    check(named(u"Luig Luigis") == u"Luig Marios", "a part of the name is left");
    check(named(u"") == u"", "empty");

    // The game's line: Luigi's face, then his name in green; it becomes Mario's face and his name in red.
    const std::u16string line = u"lost with " + picture(28) + u" " + color(2) + u"Luigi" + color(0) + u"...";
    const std::u16string want = u"lost with " + picture(18) + u" " + color(1) + u"Mario" + color(0) + u"...";
    check(named(line) == want, "face, color and name");

    // Other tags stay as they are, even one whose data reads like the name.
    const std::u16string star = picture(7) + color(3) + u"Power Star" + color(0);
    check(named(star) == star, "other tags whole");

    // A copy that would not fit is refused, not cut.
    check(named(u"with Luigi,", 11) == u"<none>", "too long");
    check(named(u"with Luigi,", 12) == u"with Mario,", "just fits");
    char16_t dst[8];
    check(NebulaLuigiText::nameMario(dst, 8, u"Luigi", u"", u"Mario") < 0, "no name");

    if (sFailures == 0) {
        printf("luigi text tests passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
