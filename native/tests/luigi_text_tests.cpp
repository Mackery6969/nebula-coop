// NebulaLuigiText::dropPagesNaming: the pages of a talk that name Luigi left out, the others kept whole.
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

    std::u16string dropped(const std::u16string& rSrc, s32 capacity = 256) {
        char16_t dst[512];
        const s32 length = NebulaLuigiText::dropPagesNaming(dst, capacity, rSrc.c_str(), u"Luigi");
        return length < 0 ? u"<none>" : std::u16string(dst, length);
    }

    // The next page (<1:1>).
    const std::u16string cPage = {cMessageTagEscape, cMessageTagNextPageHead, cMessageTagNextPage};

    // A picture tag (escape, head: group and 6 bytes, the picture's number).
    std::u16string picture(char16_t number) {
        return {cMessageTagEscape, makeMessageTagHead(MessageTagGroup_Picture, 6), number};
    }

    // A text color tag (group 255, number 0, the color's index in the parameter's high byte).
    std::u16string color(u8 index) {
        return {cMessageTagEscape, makeMessageTagHead(MessageTagGroup_System, 8), 0, static_cast< char16_t >(index << 8)};
    }
}  // namespace

int main() {
    // Captain Toad's talk: the middle page goes, the pages either side stay with the page break between them.
    const std::u16string first = u"Oh, wow! We escaped too!" + cPage;
    const std::u16string lost = u"\nBut then we got lost with\n" + picture(28) + u" " + color(2) + u"Luigi" + color(0) + u"..." + cPage;
    const std::u16string last = u"\n...Oh! We found a " + color(1) + picture(7) + u" Power Star" + color(0) + u"!";
    check(dropped(first + lost + last) == first + last, "the middle page");

    // A last page that names him takes the page break before it.
    check(dropped(first + u"\nwith Luigi...") == u"Oh, wow! We escaped too!", "the last page");
    check(dropped(lost + last) == last, "the first page");

    // Pages that don't name him, and tags whose data reads like his name, stay.
    check(dropped(first + last) == first + last, "nothing to drop");
    check(dropped(u"Luig and Luigis") == u"<none>", "a longer word naming him still counts");
    check(dropped(u"Luig") == u"Luig", "part of the name is not the name");
    check(dropped(u"Luigi") == u"<none>", "every page naming him leaves the line whole");
    check(dropped(u"") == u"<none>", "empty");

    // A copy that would not fit is refused, not cut.
    check(dropped(first + lost + last, static_cast< s32 >((first + last).size())) == u"<none>", "too long");
    check(dropped(first + lost + last, static_cast< s32 >((first + last).size()) + 1) == first + last, "just fits");
    char16_t dst[8];
    check(NebulaLuigiText::dropPagesNaming(dst, 8, u"Luigi", u"") < 0, "no name");

    if (sFailures == 0) {
        printf("luigi text tests passed\n");
    }
    return sFailures == 0 ? 0 : 1;
}
