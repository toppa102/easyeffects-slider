/*
 *  Copyright © 2017-2023 Wellington Wallace
 *
 *  This file is part of Easy Effects.
 *
 *  Easy Effects is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Easy Effects is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Easy Effects. If not, see <https://www.gnu.org/licenses/>.
 */

#include "presets_menu.hpp"

namespace ui::presets_menu {

struct Data {
 public:
  ~Data() { util::debug("data struct destroyed"); }

  app::Application* application;

  PresetType preset_type;

  std::vector<sigc::connection> connections;

  std::vector<gulong> gconnections;
};

struct _PresetsMenu {
  GtkPopover parent_instance;

  AdwViewStack* stack;

  GtkScrolledWindow* scrolled_window;

  GtkListView* listview;

  GtkText* name;

  GtkLabel* last_used_name;

  GtkStringList* string_list;

  GSettings* settings;

  Data* data;
};

// NOLINTNEXTLINE
G_DEFINE_TYPE(PresetsMenu, presets_menu, GTK_TYPE_POPOVER)

void create_preset(PresetsMenu* self, GtkButton* button) {
  auto name = std::string(g_utf8_make_valid(gtk_editable_get_text(GTK_EDITABLE(self->name)), -1));

  if (name.empty()) {
    return;
  }

  gtk_editable_set_text(GTK_EDITABLE(self->name), "");

  // Truncate if longer than 100 characters

  if (name.size() > 100U) {
    name.resize(100U);
  }

  if (name.find_first_of("\\/") != std::string::npos) {
    util::debug(" name " + name + " has illegal file name characters. Aborting preset creation!");

    return;
  }

  self->data->application->presets_manager->add(self->data->preset_type, name);
}

void import_preset(PresetsMenu* self) {
  auto* active_window = gtk_application_get_active_window(GTK_APPLICATION(self->data->application));

  auto* dialog = gtk_file_dialog_new();

  gtk_file_dialog_set_title(dialog, _("Import Preset"));
  gtk_file_dialog_set_accept_label(dialog, _("Open"));

  auto* init_folder = g_file_new_for_path(SYSTEM_PRESETS_DIR);

  gtk_file_dialog_set_initial_folder(dialog, init_folder);

  g_object_unref(init_folder);

  GListStore* filters = g_list_store_new(GTK_TYPE_FILE_FILTER);

  auto* filter = gtk_file_filter_new();

  gtk_file_filter_add_pattern(filter, "*.json");
  gtk_file_filter_set_name(filter, _("Presets"));

  g_list_store_append(filters, filter);

  g_object_unref(filter);

  gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

  g_object_unref(filters);

  gtk_file_dialog_open_multiple(
      dialog, active_window, nullptr,
      +[](GObject* source_object, GAsyncResult* result, gpointer user_data) {
        auto* self = static_cast<PresetsMenu*>(user_data);
        auto* dialog = GTK_FILE_DIALOG(source_object);

        auto* files_list = gtk_file_dialog_open_multiple_finish(dialog, result, nullptr);

        if (files_list == nullptr) {
          return;
        }

        for (guint n = 0U; n < g_list_model_get_n_items(files_list); n++) {
          auto* file = static_cast<GFile*>(g_list_model_get_item(files_list, n));
          auto* path = g_file_get_path(file);

          if (self->data->preset_type == PresetType::output) {
            self->data->application->presets_manager->import(PresetType::output, path);
          } else if (self->data->preset_type == PresetType::input) {
            self->data->application->presets_manager->import(PresetType::input, path);
          }

          g_free(path);
        }

        g_object_unref(files_list);
      },
      self);
}

template <PresetType preset_type>
void setup_listview(PresetsMenu* self, GtkListView* listview, GtkStringList* string_list) {
  auto* factory = gtk_signal_list_item_factory_new();

  // setting the factory callbacks

  g_signal_connect(
      factory, "setup", G_CALLBACK(+[](GtkSignalListItemFactory* factory, GtkListItem* item, PresetsMenu* self) {
        auto builder = gtk_builder_new_from_resource(tags::resources::preset_row_ui);

        auto* top_box = gtk_builder_get_object(builder, "top_box");
        auto* apply = gtk_builder_get_object(builder, "apply");
        auto* save = gtk_builder_get_object(builder, "save");
        auto* remove = gtk_builder_get_object(builder, "remove");

        auto* confirmation_box = gtk_builder_get_object(builder, "confirmation_box");
        auto* confirmation_label = gtk_builder_get_object(builder, "confirmation_label");
        auto* confirmation_yes = gtk_builder_get_object(builder, "confirmation_yes");
        auto* confirmation_no = gtk_builder_get_object(builder, "confirmation_no");

        g_object_set_data(G_OBJECT(item), "name", gtk_builder_get_object(builder, "name"));
        g_object_set_data(G_OBJECT(item), "apply", apply);
        g_object_set_data(G_OBJECT(item), "confirmation_yes", confirmation_yes);

        g_object_set_data(G_OBJECT(save), "confirmation_box", confirmation_box);
        g_object_set_data(G_OBJECT(save), "confirmation_label", confirmation_label);
        g_object_set_data(G_OBJECT(save), "confirmation_yes", confirmation_yes);

        g_object_set_data(G_OBJECT(remove), "confirmation_box", confirmation_box);
        g_object_set_data(G_OBJECT(remove), "confirmation_label", confirmation_label);
        g_object_set_data(G_OBJECT(remove), "confirmation_yes", confirmation_yes);

        g_object_set_data(G_OBJECT(confirmation_yes), "confirmation_box", confirmation_box);
        g_object_set_data(G_OBJECT(confirmation_yes), "confirmation_label", confirmation_label);

        g_object_set_data(G_OBJECT(confirmation_no), "confirmation_label", confirmation_label);

        gtk_list_item_set_activatable(item, 0);
        gtk_list_item_set_child(item, GTK_WIDGET(top_box));

        g_signal_connect(
            apply, "clicked", G_CALLBACK(+[](GtkButton* button, PresetsMenu* self) {
              if (auto* string_object = GTK_STRING_OBJECT(g_object_get_data(G_OBJECT(button), "string-object"));
                  string_object != nullptr) {
                auto* preset_name = gtk_string_object_get_string(string_object);

                if constexpr (preset_type == PresetType::output) {
                  if (self->data->application->presets_manager->load_preset_file(PresetType::output, preset_name)) {
                    g_settings_set_string(self->settings, "last-used-output-preset", preset_name);
                  } else {
                    g_settings_reset(self->settings, "last-used-output-preset");
                  }
                } else if constexpr (preset_type == PresetType::input) {
                  if (self->data->application->presets_manager->load_preset_file(PresetType::input, preset_name)) {
                    g_settings_set_string(self->settings, "last-used-input-preset", preset_name);
                  } else {
                    g_settings_reset(self->settings, "last-used-input-preset");
                  }
                }
              }
            }),
            self);

        g_signal_connect(
            save, "clicked", G_CALLBACK(+[](GtkButton* button, PresetsMenu* self) {
              auto* confirmation_box = static_cast<GtkBox*>(g_object_get_data(G_OBJECT(button), "confirmation_box"));

              auto* confirmation_label =
                  static_cast<GtkLabel*>(g_object_get_data(G_OBJECT(button), "confirmation_label"));

              auto* confirmation_yes = static_cast<GtkLabel*>(g_object_get_data(G_OBJECT(button), "confirmation_yes"));

              gtk_label_set_text(confirmation_label, _("Save?"));

              gtk_widget_add_css_class(GTK_WIDGET(confirmation_label), "warning");

              gtk_widget_set_visible(GTK_WIDGET(confirmation_box), 1);

              g_object_set_data(G_OBJECT(confirmation_yes), "operation", GUINT_TO_POINTER(0));
            }),
            self);

        g_signal_connect(
            remove, "clicked", G_CALLBACK(+[](GtkButton* button, PresetsMenu* self) {
              auto* confirmation_box = static_cast<GtkBox*>(g_object_get_data(G_OBJECT(button), "confirmation_box"));

              auto* confirmation_label =
                  static_cast<GtkLabel*>(g_object_get_data(G_OBJECT(button), "confirmation_label"));

              auto* confirmation_yes = static_cast<GtkLabel*>(g_object_get_data(G_OBJECT(button), "confirmation_yes"));

              gtk_label_set_text(confirmation_label, _("Delete?"));

              gtk_widget_add_css_class(GTK_WIDGET(confirmation_label), "error");

              gtk_widget_set_visible(GTK_WIDGET(confirmation_box), 1);

              g_object_set_data(G_OBJECT(confirmation_yes), "operation", GUINT_TO_POINTER(1));
            }),
            self);

        g_signal_connect(confirmation_no, "clicked", G_CALLBACK(+[](GtkButton* button, GtkBox* box) {
                           gtk_widget_set_visible(GTK_WIDGET(box), 0);

                           auto* confirmation_label =
                               static_cast<GtkBox*>(g_object_get_data(G_OBJECT(button), "confirmation_label"));

                           gtk_widget_remove_css_class(GTK_WIDGET(confirmation_label), "warning");
                           gtk_widget_remove_css_class(GTK_WIDGET(confirmation_label), "error");
                         }),
                         confirmation_box);

        g_signal_connect(
            confirmation_yes, "clicked", G_CALLBACK(+[](GtkButton* button, PresetsMenu* self) {
              if (auto* string_object = GTK_STRING_OBJECT(g_object_get_data(G_OBJECT(button), "string-object"));
                  string_object != nullptr) {
                auto* preset_name = gtk_string_object_get_string(string_object);

                uint operation = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "operation"));

                auto* confirmation_label =
                    static_cast<GtkBox*>(g_object_get_data(G_OBJECT(button), "confirmation_label"));

                switch (operation) {
                  case 0: {  // save
                    if constexpr (preset_type == PresetType::output) {
                      self->data->application->presets_manager->save_preset_file(PresetType::output, preset_name);
                    } else if constexpr (preset_type == PresetType::input) {
                      self->data->application->presets_manager->save_preset_file(PresetType::input, preset_name);
                    }

                    gtk_widget_remove_css_class(GTK_WIDGET(confirmation_label), "warning");

                    break;
                  }
                  case 1: {  // delete
                    if constexpr (preset_type == PresetType::output) {
                      self->data->application->presets_manager->remove(PresetType::output, preset_name);
                    } else if constexpr (preset_type == PresetType::input) {
                      self->data->application->presets_manager->remove(PresetType::input, preset_name);
                    }

                    gtk_widget_remove_css_class(GTK_WIDGET(confirmation_label), "error");

                    break;
                  }
                }
              }

              auto* confirmation_box = static_cast<GtkBox*>(g_object_get_data(G_OBJECT(button), "confirmation_box"));

              gtk_widget_set_visible(GTK_WIDGET(confirmation_box), 0);
            }),
            self);

        g_object_unref(builder);
      }),
      self);

  g_signal_connect(
      factory, "bind", G_CALLBACK(+[](GtkSignalListItemFactory* factory, GtkListItem* item, PresetsMenu* self) {
        auto* label = static_cast<GtkLabel*>(g_object_get_data(G_OBJECT(item), "name"));
        auto* apply = static_cast<GtkButton*>(g_object_get_data(G_OBJECT(item), "apply"));
        auto* confirmation_yes = static_cast<GtkButton*>(g_object_get_data(G_OBJECT(item), "confirmation_yes"));

        auto* string_object = GTK_STRING_OBJECT(gtk_list_item_get_item(item));

        g_object_set_data(G_OBJECT(apply), "string-object", string_object);
        g_object_set_data(G_OBJECT(confirmation_yes), "string-object", string_object);

        auto* name = gtk_string_object_get_string(string_object);

        gtk_label_set_text(label, name);
      }),
      self);

  gtk_list_view_set_factory(listview, factory);

  g_object_unref(factory);

  for (const auto& name : self->data->application->presets_manager->get_names(preset_type)) {
    gtk_string_list_append(string_list, name.c_str());
  }
}

void setup(PresetsMenu* self, app::Application* application, PresetType preset_type) {
  self->data->application = application;
  self->data->preset_type = preset_type;

  auto add_to_list = [=](const std::string& preset_name) {
    if (preset_name.empty()) {
      util::warning("can't retrieve information about the preset file");

      return;
    }

    for (guint n = 0U; n < g_list_model_get_n_items(G_LIST_MODEL(self->string_list)); n++) {
      if (preset_name == gtk_string_list_get_string(self->string_list, n)) {
        return;
      }
    }

    gtk_string_list_append(self->string_list, preset_name.c_str());
  };

  auto remove_from_list = [=](const std::string& preset_name) {
    if (preset_name.empty()) {
      util::warning("can't retrieve information about the preset file");

      return;
    }

    for (guint n = 0U; n < g_list_model_get_n_items(G_LIST_MODEL(self->string_list)); n++) {
      if (preset_name == gtk_string_list_get_string(self->string_list, n)) {
        gtk_string_list_remove(self->string_list, n);

        return;
      }
    }
  };

  if (preset_type == PresetType::output) {
    setup_listview<PresetType::output>(self, self->listview, self->string_list);

    self->data->connections.push_back(
        self->data->application->presets_manager->user_output_preset_created.connect(add_to_list));

    self->data->connections.push_back(
        self->data->application->presets_manager->user_output_preset_removed.connect(remove_from_list));

    self->data->gconnections.push_back(
        g_signal_connect(self->settings, "changed::last-used-output-preset",
                         G_CALLBACK(+[](GSettings* settings, char* key, gpointer user_data) {
                           auto* self = static_cast<PresetsMenu*>(user_data);

                           gtk_label_set_text(self->last_used_name, util::gsettings_get_string(settings, key).c_str());
                         }),
                         self));

    gtk_label_set_text(self->last_used_name,
                       util::gsettings_get_string(self->settings, "last-used-output-preset").c_str());

    // reset last used name label

    const auto names_output = self->data->application->presets_manager->get_names(PresetType::output);

    if (names_output.empty()) {
      g_settings_set_string(self->settings, "last-used-output-preset", _("Presets"));

      return;
    }

    for (const auto& name : names_output) {
      if (name == util::gsettings_get_string(self->settings, "last-used-output-preset")) {
        return;
      }
    }

    g_settings_set_string(self->settings, "last-used-output-preset", _("Presets"));

  } else if (preset_type == PresetType::input) {
    setup_listview<PresetType::input>(self, self->listview, self->string_list);

    self->data->connections.push_back(
        self->data->application->presets_manager->user_input_preset_created.connect(add_to_list));

    self->data->connections.push_back(
        self->data->application->presets_manager->user_input_preset_removed.connect(remove_from_list));

    self->data->gconnections.push_back(
        g_signal_connect(self->settings, "changed::last-used-input-preset",
                         G_CALLBACK(+[](GSettings* settings, char* key, gpointer user_data) {
                           auto* self = static_cast<PresetsMenu*>(user_data);

                           gtk_label_set_text(self->last_used_name, util::gsettings_get_string(settings, key).c_str());
                         }),
                         self));

    gtk_label_set_text(self->last_used_name,
                       util::gsettings_get_string(self->settings, "last-used-input-preset").c_str());

    // reset last used name label

    const auto names_input = self->data->application->presets_manager->get_names(PresetType::input);

    if (names_input.empty()) {
      g_settings_set_string(self->settings, "last-used-input-preset", _("Presets"));

      return;
    }

    for (const auto& name : names_input) {
      if (name == util::gsettings_get_string(self->settings, "last-used-input-preset")) {
        return;
      }
    }

    g_settings_set_string(self->settings, "last-used-input-preset", _("Presets"));
  }
}

void show(GtkWidget* widget) {
  auto* self = EE_PRESETS_MENU(widget);

  auto* active_window = gtk_application_get_active_window(GTK_APPLICATION(self->data->application));

  auto active_window_height = gtk_widget_get_height(GTK_WIDGET(active_window));

  const int menu_height = static_cast<int>(0.5F * static_cast<float>(active_window_height));

  gtk_scrolled_window_set_max_content_height(self->scrolled_window, menu_height);

  GTK_WIDGET_CLASS(presets_menu_parent_class)->show(widget);
}

void dispose(GObject* object) {
  auto* self = EE_PRESETS_MENU(object);

  for (auto& c : self->data->connections) {
    c.disconnect();
  }

  for (auto& handler_id : self->data->gconnections) {
    g_signal_handler_disconnect(self->settings, handler_id);
  }

  self->data->connections.clear();
  self->data->gconnections.clear();

  g_object_unref(self->settings);

  util::debug("disposed");

  G_OBJECT_CLASS(presets_menu_parent_class)->dispose(object);
}

void finalize(GObject* object) {
  auto* self = EE_PRESETS_MENU(object);

  delete self->data;

  util::debug("finalized");

  G_OBJECT_CLASS(presets_menu_parent_class)->finalize(object);
}

void presets_menu_class_init(PresetsMenuClass* klass) {
  auto* object_class = G_OBJECT_CLASS(klass);
  auto* widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = dispose;
  object_class->finalize = finalize;

  widget_class->show = show;

  gtk_widget_class_set_template_from_resource(widget_class, tags::resources::presets_menu_ui);

  gtk_widget_class_bind_template_child(widget_class, PresetsMenu, string_list);

  gtk_widget_class_bind_template_child(widget_class, PresetsMenu, scrolled_window);
  gtk_widget_class_bind_template_child(widget_class, PresetsMenu, listview);
  gtk_widget_class_bind_template_child(widget_class, PresetsMenu, name);
  gtk_widget_class_bind_template_child(widget_class, PresetsMenu, last_used_name);

  gtk_widget_class_bind_template_callback(widget_class, create_preset);
  gtk_widget_class_bind_template_callback(widget_class, import_preset);
}

void presets_menu_init(PresetsMenu* self) {
  gtk_widget_init_template(GTK_WIDGET(self));

  self->data = new Data();

  self->settings = g_settings_new(tags::app::id);
}

auto create() -> PresetsMenu* {
  return static_cast<PresetsMenu*>(g_object_new(EE_TYPE_PRESETS_MENU, nullptr));
}

}  // namespace ui::presets_menu
