// HelpDialog.h —— 帮助 / 使用教程（v3.3.3 任务书：面向小白的帮助教程）
//
// 本文件只对外提供两个自由函数，不定义任何类、不使用 Q_OBJECT，
// 因此无需（也不应）在 .cpp 中 include "HelpDialog.moc"：
//
//   helpTutorialHtml()  —— 返回教程正文（HTML 字符串）。
//                          GUI 自检与弹窗共用同一份文本，自检会断言
//                          各章节标题均存在，避免“文档与实际界面不一致”。
//   showHelpDialog()    —— 弹出「使用教程」模态对话框：
//                          正文可滚动、可用鼠标/键盘选中复制，并提供
//                          「复制全部文字」与「关闭」两个按钮。
//
// 教程章节（顺序固定，供自检匹配）：
//   【这是什么】【怎么用】【图案怎么画】【四种模式怎么选】
//   【GPU 加速是什么】【结果怎么用】【常见问题】
#pragma once

#include <QString>

class QWidget;

// 帮助 / 使用教程正文（HTML，供对话框与 GUI 自检共用；自检会断言各章节标题存在）
QString helpTutorialHtml();

// 弹出模态帮助对话框（内容可滚动、可复制）
void showHelpDialog(QWidget* parent);
