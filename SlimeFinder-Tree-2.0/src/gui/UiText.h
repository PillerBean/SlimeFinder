#pragma once

#include <QString>

namespace slime {
namespace ui {

// v3.11.0：所有用户可见文本的运行时入口。
// 语言包位于 <程序目录>/assets/i18n/<lang>.json；代码里只出现 key。
QString t(const char* key);
void initTextCatalog();

} // namespace ui
} // namespace slime
