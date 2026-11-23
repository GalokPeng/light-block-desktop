#pragma once
#include "sqlite.hpp"
#include <optional>
#include "clipboard_ui_api.h"
namespace lb::clipboard {
struct Item { int64_t id{},created{};int kind{};std::wstring text;std::vector<BYTE> image;bool pinned{}; };
class Store {
    sql::Database database_;
public:
    explicit Store(std::filesystem::path const& path);
    bool add(Item const& item);
    std::vector<Item> list(std::wstring const& query);
    std::optional<Item> get(int64_t id);
    void toggle_pin(int64_t id);
    void remove(int64_t id);
    void clear();
};
bool configure();
void request_image_editor();
bool shutdown();
bool message(UINT message,WPARAM wp,LPARAM lp);
void show();
void layout();
void home();
HWND embed(HWND parent);
ClipboardUiApi const* ui_api();
bool tests(std::filesystem::path const& path);
}
