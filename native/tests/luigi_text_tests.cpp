// NebulaLuigiText::replaceName: Mario's name for Luigi's in the game's text, tags left whole.
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

    std::u16string replaced(const std::u16string& rSrc, s32 capacity = 64) {
        char16_t dst[256];
        const s32 length = NebulaLuigiText::replaceName(dst, capacity, rSrc.c_str(), u"Mario", u"Luigi");
        return length < 0 ? u"<none>" : std::u16string(dst, length);
    }
}  // namespace

int main() {
    check(replaced(u"Dear Mario,") == u"Dear Luigi,", "the letter");
    check(replaced(u"Mario!") == u"Luigi!", "Peach's call");
    check(replaced(u"Querido Mario,") == u"Querido Luigi,", "Spanish");
    check(replaced(u"Mari Mario Mar") == u"Mari Luigi Mar", "a part of the name is left");
    check(replaced(u"Mario & Mario") == u"Luigi & Luigi", "each one");
    check(replaced(u"") == u"", "empty");

    // A tag (escape, head: group 3, 6 bytes, then its number) whose data reads "Mario" stays as it is.
    std::u16string tag = u"\x1A";
    tag += static_cast< char16_t >(makeMessageTagHead(MessageTagGroup_Picture, 6));
    tag += u'M';
    check(replaced(tag + u"Mario") == tag + u"Luigi", "tags whole");

    // A copy that would not fit is refused, not cut.
    check(replaced(u"Dear Mario,", 11) == u"<none>", "too long");
    check(replaced(u"Dear Mario,", 12) == u"Dear Luigi,", "just fits");
    char16_t dst[8];
    check(NebulaLuigiText::replaceName(dst, 8, u"Mario", u"", u"Luigi") < 0, "no name");

    if (sFailures == 0) {
        printf("luigi text tests passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
