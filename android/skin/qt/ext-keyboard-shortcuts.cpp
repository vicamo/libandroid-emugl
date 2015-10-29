/* Copyright (C) 2015 The Android Open Source Project
 **
 ** This software is licensed under the terms of the GNU General Public
 ** License version 2, as published by the Free Software Foundation, and
 ** may be copied, distributed, and modified under those terms.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 */

#include "android/skin/qt/extended-window.h"

#include "android/emulation/control/window_agent.h"
#include "android/skin/keyset.h"
#include "android/skin/ui.h"
#include "ui_extended.h"

static void addShortcutsTableRow(QTableWidget* table_widget,
                                 const QString& key_sequence,
                                 const QString& description) {
    int table_row = table_widget->rowCount();
    table_widget->insertRow(table_row);
    table_widget->setItem(table_row, 0, new QTableWidgetItem(description));
    table_widget->setItem(table_row, 1, new QTableWidgetItem(key_sequence));
}

void ExtendedWindow::initKbdShorts() {
    EmulatorWindow* emu_win = mEmulatorWindow->getEmulatorWindow();
    SkinKeyset* kset = mEmulatorWindow->getCurrentKeyset(emu_win->ui);
    SkinKeyBinding bindings[SKIN_KEY_COMMAND_MAX_BINDINGS];
    QTableWidget* table_widget = mExtendedUi->shortcutsTableWidget;

    for (int cmd = SKIN_KEY_COMMAND_NONE + 1;
         cmd < SKIN_KEY_COMMAND_MAX;
         ++cmd) {
        int bindings_count =
            skin_keyset_get_bindings(
                kset,
                static_cast<SkinKeyCommand>(cmd),
                bindings);
        if (bindings_count > 0) {
            QString binding_str;
            for (int binding_idx = 0; binding_idx < bindings_count; ++binding_idx) {
                SkinKeyBinding& binding = bindings[binding_idx];
                binding_str += skin_key_pair_to_string(binding.sym, binding.mod);
                if (binding_idx < bindings_count - 1) {
                   binding_str += ",";
                }
            }
            // Unfortunately, string representations of the non-Qt specific
            // shortcuts use "-" rather than "+", which is used by QKeySequence.
            // Replace with + so that it appears consistent in the UI. Once we
            // start handling all keyboard shortcut related things in Qt UI,
            // this will no longer be necessary.
            binding_str.replace(QString("-"), QString("+"));

            QString description(skin_key_command_description(static_cast<SkinKeyCommand>(cmd)));
            addShortcutsTableRow(table_widget, binding_str, description);
        }
    }

    // Now add the Qt UI specific keyboard shortcuts
    if (mQtUIShortcuts) {
        for (const auto& key_sequence_and_command : *mQtUIShortcuts) {
            addShortcutsTableRow(table_widget,
                                 key_sequence_and_command.first.toString(),
                                 getQtUICommandDescription(key_sequence_and_command.second));
        }
    }

    table_widget->sortItems(0);
}

