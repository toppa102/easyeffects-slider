#include "preferences_window.hpp"

namespace ui::preferences_window {

using namespace std::string_literals;

auto constexpr log_tag = "preferences_window: ";

struct _PreferencesWindow {
  AdwPreferencesWindow parent_instance{};

  GtkSwitch *enable_autostart = nullptr, *process_all_inputs = nullptr, *process_all_outputs = nullptr,
            *theme_switch = nullptr, *shutdown_on_window_close = nullptr, *use_cubic_volumes = nullptr,
            *spectrum_show = nullptr, *spectrum_fill = nullptr, *spectrum_show_bar_border = nullptr;

  GtkColorButton *spectrum_color_button = nullptr, *spectrum_axis_color_button = nullptr;

  GtkComboBoxText* spectrum_type = nullptr;

  GtkSpinButton *spectrum_n_points = nullptr, *spectrum_height = nullptr, *spectrum_line_width = nullptr,
                *spectrum_minimum_frequency = nullptr, *spectrum_maximum_frequency = nullptr;

  GSettings* settings = nullptr;
  GSettings* settings_spectrum = nullptr;

  std::vector<gulong> gconnections_spectrum;
};

G_DEFINE_TYPE(PreferencesWindow, preferences_window, ADW_TYPE_PREFERENCES_WINDOW)

auto on_enable_autostart(GtkSwitch* obj, gboolean state, gpointer user_data) -> gboolean {
  std::filesystem::path autostart_dir{g_get_user_config_dir() + "/autostart"s};

  if (!std::filesystem::is_directory(autostart_dir)) {
    std::filesystem::create_directories(autostart_dir);
  }

  std::filesystem::path autostart_file{g_get_user_config_dir() + "/autostart/easyeffects-service.desktop"s};

  if (state != 0) {
    if (!std::filesystem::exists(autostart_file)) {
      std::ofstream ofs{autostart_file};

      ofs << "[Desktop Entry]\n";
      ofs << "Name=EasyEffects\n";
      ofs << "Comment=EasyEffects Service\n";
      ofs << "Exec=easyeffects --gapplication-service\n";
      ofs << "Icon=easyeffects\n";
      ofs << "StartupNotify=false\n";
      ofs << "Terminal=false\n";
      ofs << "Type=Application\n";

      ofs.close();

      util::debug(log_tag + "autostart file created"s);
    }
  } else {
    if (std::filesystem::exists(autostart_file)) {
      std::filesystem::remove(autostart_file);

      util::debug(log_tag + "autostart file removed"s);
    }
  }

  return 0;
}

void on_spectrum_color_set(PreferencesWindow* self, GtkColorButton* button) {
  GdkRGBA rgba;

  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);

  g_settings_set(self->settings_spectrum, "color", "(dddd)", rgba.red, rgba.green, rgba.blue, rgba.alpha);
}

void on_spectrum_axis_color_set(PreferencesWindow* self, GtkColorButton* button) {
  GdkRGBA rgba;

  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);

  g_settings_set(self->settings_spectrum, "color-axis-labels", "(dddd)", rgba.red, rgba.green, rgba.blue, rgba.alpha);
}

void dispose(GObject* object) {
  auto* self = EE_PREFERENCES_WINDOW(object);

  for (auto& handler_id : self->gconnections_spectrum) {
    g_signal_handler_disconnect(self->settings_spectrum, handler_id);
  }

  util::debug(log_tag + "destroyed"s);

  G_OBJECT_CLASS(preferences_window_parent_class)->dispose(object);
}

void preferences_window_class_init(PreferencesWindowClass* klass) {
  auto* widget_class = GTK_WIDGET_CLASS(klass);

  gtk_widget_class_set_template_from_resource(widget_class, "/com/github/wwmm/easyeffects/ui/preferences_window.ui");

  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, enable_autostart);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, process_all_inputs);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, process_all_outputs);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, theme_switch);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, shutdown_on_window_close);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, use_cubic_volumes);

  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_show);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_type);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_fill);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_n_points);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_line_width);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_height);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_show_bar_border);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_color_button);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_axis_color_button);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_minimum_frequency);
  gtk_widget_class_bind_template_child(widget_class, PreferencesWindow, spectrum_maximum_frequency);

  gtk_widget_class_bind_template_callback(widget_class, on_enable_autostart);
  gtk_widget_class_bind_template_callback(widget_class, on_spectrum_color_set);
  gtk_widget_class_bind_template_callback(widget_class, on_spectrum_axis_color_set);
}

void preferences_window_init(PreferencesWindow* self) {
  gtk_widget_init_template(GTK_WIDGET(self));

  self->settings = g_settings_new("com.github.wwmm.easyeffects");
  self->settings_spectrum = g_settings_new("com.github.wwmm.easyeffects.spectrum");

  // initializing some widgets

  gtk_switch_set_active(self->enable_autostart,
                        static_cast<gboolean>(std::filesystem::is_regular_file(
                            g_get_user_config_dir() + "/autostart/easyeffects-service.desktop"s)));

  auto color = util::gsettings_get_color(self->settings_spectrum, "color");

  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(self->spectrum_color_button), &color);

  color = util::gsettings_get_color(self->settings_spectrum, "color-axis-labels");

  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(self->spectrum_axis_color_button), &color);

  // general section gsettings bindings

  g_settings_bind(self->settings, "use-dark-theme", self->theme_switch, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings, "process-all-inputs", self->process_all_inputs, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings, "process-all-outputs", self->process_all_outputs, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings, "shutdown-on-window-close", self->shutdown_on_window_close, "active",
                  G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings, "use-cubic-volumes", self->use_cubic_volumes, "active", G_SETTINGS_BIND_DEFAULT);

  // spectrum section gsettings bindings

  g_settings_bind(self->settings_spectrum, "show", self->spectrum_show, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings_spectrum, "fill", self->spectrum_fill, "active", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings_spectrum, "show-bar-border", self->spectrum_show_bar_border, "active",
                  G_SETTINGS_BIND_DEFAULT);

  g_settings_bind(self->settings_spectrum, "n-points", gtk_spin_button_get_adjustment(self->spectrum_n_points), "value",
                  G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings_spectrum, "height", gtk_spin_button_get_adjustment(self->spectrum_height), "value",
                  G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings_spectrum, "line-width", gtk_spin_button_get_adjustment(self->spectrum_line_width),
                  "value", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings_spectrum, "minimum-frequency",
                  gtk_spin_button_get_adjustment(self->spectrum_minimum_frequency), "value", G_SETTINGS_BIND_DEFAULT);
  g_settings_bind(self->settings_spectrum, "maximum-frequency",
                  gtk_spin_button_get_adjustment(self->spectrum_maximum_frequency), "value", G_SETTINGS_BIND_DEFAULT);

  g_settings_bind_with_mapping(
      self->settings_spectrum, "type", self->spectrum_type, "active", G_SETTINGS_BIND_DEFAULT,
      +[](GValue* value, GVariant* variant, gpointer user_data) {
        const auto* v = g_variant_get_string(variant, nullptr);

        if (g_strcmp0(v, "Bars") == 0) {
          g_value_set_int(value, 0);
        } else if (g_strcmp0(v, "Lines") == 0) {
          g_value_set_int(value, 1);
        }

        return 1;
      },
      +[](const GValue* value, const GVariantType* expected_type, gpointer user_data) {
        switch (g_value_get_int(value)) {
          case 0:
            return g_variant_new_string("Bars");

          case 1:
            return g_variant_new_string("Lines");

          default:
            return g_variant_new_string("Bars");
        }
      },
      nullptr, nullptr);

  // Spectrum gsettings signals connections

  self->gconnections_spectrum.push_back(
      g_signal_connect(self->settings_spectrum, "changed::color",
                       G_CALLBACK((+[](GSettings* settings, char* key, PreferencesWindow* self) {
                         auto color = util::gsettings_get_color(self->settings_spectrum, key);

                         gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(self->spectrum_color_button), &color);
                       })),
                       self));

  self->gconnections_spectrum.push_back(
      g_signal_connect(self->settings_spectrum, "changed::color-axis-labels",
                       G_CALLBACK((+[](GSettings* settings, char* key, PreferencesWindow* self) {
                         auto color = util::gsettings_get_color(self->settings_spectrum, key);

                         gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(self->spectrum_axis_color_button), &color);
                       })),
                       self));
}

auto create() -> PreferencesWindow* {
  return static_cast<PreferencesWindow*>(g_object_new(EE_TYPE_PREFERENCES_WINDOW, nullptr));
}

}  // namespace ui::preferences_window