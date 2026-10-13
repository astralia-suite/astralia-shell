#include <string>

#include "check.h"

#include "render/text_field.h"

void check_text_cursor() {
    using namespace astralia;

    std::string text = "añb c";
    TextCursor cursor;
    test::check(!cursor.has_selection() && cursor.begin() == 0 && cursor.end() == 0, "a new cursor is empty at the start");

    text_cursor_end(text, cursor, false);
    test::check(cursor.pos == text.size() && cursor.anchor == text.size(), "end moves both ends");
    text_cursor_move(text, cursor, CursorDir::Left, false);
    text_cursor_move(text, cursor, CursorDir::Left, false);
    text_cursor_move(text, cursor, CursorDir::Left, false);
    test::check(cursor.pos == 3, "moving left steps over characters");
    text_cursor_move(text, cursor, CursorDir::Left, false);
    test::check(cursor.pos == 1, "moving left steps over a multibyte character");
    text_cursor_move(text, cursor, CursorDir::Right, true);
    test::check(cursor.pos == 3 && cursor.anchor == 1 && cursor.has_selection(), "extending right keeps the anchor");
    test::check(text_cursor_selected_text(text, cursor) == "ñ", "the selection is a whole character");
    text_cursor_move(text, cursor, CursorDir::Left, false);
    test::check(cursor.pos == 1 && !cursor.has_selection(), "moving collapses the selection to its start");

    TextCursor stale{99, 99};
    text_cursor_clamp(text, stale);
    test::check(stale.pos == text.size() && stale.anchor == text.size(), "a stale cursor is clamped");

    TextCursor inside;
    text_cursor_set(text, inside, 2, false);
    test::check(inside.pos == 1, "setting inside a character snaps back to its start");
    text_cursor_set(text, inside, 100, true);
    test::check(inside.pos == text.size() && inside.anchor == 1, "setting past the end clamps and extends");

    TextCursor words;
    text_cursor_end(text, words, false);
    text_cursor_move_word(text, words, CursorDir::Left, false);
    test::check(words.pos == text.size() - 1, "a single letter word is skipped");
    text_cursor_move_word(text, words, CursorDir::Left, false);
    test::check(words.pos == 0, "a word with a multibyte letter is one word");
    text_cursor_move_word(text, words, CursorDir::Right, true);
    test::check(words.pos == 4 && words.anchor == 0, "word jumps extend the selection");

    TextCursor pick;
    text_cursor_select_word(text, pick, 2);
    test::check(text_cursor_selected_text(text, pick) == "añb", "selecting a word covers every byte of it");
    text_cursor_select_word(text, pick, 5);
    test::check(text_cursor_selected_text(text, pick) == "c", "selecting at the last byte works");
    text_cursor_select_word(text, pick, 4);
    test::check(text_cursor_selected_text(text, pick) == " ", "selecting a separator picks the separator run");
    TextCursor none{3, 1};
    text_cursor_select_word("", none, 0);
    test::check(none.pos == 0 && none.anchor == 0, "selecting in an empty text clears the cursor");

    std::string edit = "abcd";
    TextCursor at;
    text_cursor_set(edit, at, 2, false);
    test::check(text_cursor_insert(edit, at, "XY") && edit == "abXYcd" && at.pos == 4 && at.anchor == 4, "insert goes at the cursor and moves it");
    test::check(!text_cursor_insert(edit, at, ""), "inserting nothing changes nothing");
    text_cursor_set(edit, at, 1, false);
    text_cursor_set(edit, at, 5, true);
    test::check(text_cursor_insert(edit, at, "-") && edit == "a-d" && at.pos == 2, "insert replaces the selection");
    test::check(text_cursor_backspace(edit, at) && edit == "ad" && at.pos == 1, "backspace removes before the cursor");
    test::check(text_cursor_delete(edit, at) && edit == "a" && at.pos == 1, "delete removes after the cursor");
    test::check(!text_cursor_delete(edit, at), "delete at the end changes nothing");
    text_cursor_home(at, false);
    test::check(!text_cursor_backspace(edit, at), "backspace at the start changes nothing");
    text_cursor_select_all(edit, at);
    test::check(text_cursor_delete_selection(edit, at) && edit.empty() && at.pos == 0, "deleting a selection empties the text");
    test::check(!text_cursor_delete_selection(edit, at), "deleting without a selection changes nothing");

    std::string wide = "añ😀b";
    test::check(text_field_char_index(wide, 0) == 0 && text_field_char_index(wide, 1) == 1 && text_field_char_index(wide, 3) == 2 && text_field_char_index(wide, wide.size()) == 4, "byte offsets convert to character indexes");
    test::check(text_field_byte_offset(wide, 0) == 0 && text_field_byte_offset(wide, 2) == 3 && text_field_byte_offset(wide, 3) == 7 && text_field_byte_offset(wide, 99) == wide.size(), "character indexes convert to byte offsets");
}
