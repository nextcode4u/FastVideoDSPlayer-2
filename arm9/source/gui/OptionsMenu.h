#pragma once
namespace OptionsMenu
{
enum Action
{
    Continue,
    Back,
    Return,
    Bookmark,
    GoBookmark,
    Queue,
    Recent,
    SetRoot,
    BrowseRoot
};
Action Show(bool playing, const char *currentDirectory = 0);
int Choose(const char *title, const char *const *items, int count, int selected = 0);
void Notice(const char *message);
} // namespace OptionsMenu
