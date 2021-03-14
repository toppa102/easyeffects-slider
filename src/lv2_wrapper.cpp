#include "lv2_wrapper.hpp"

namespace lv2 {

Lv2Wrapper::Lv2Wrapper(const std::string& plugin_uri) : plugin_uri(plugin_uri) {
  world = lilv_world_new();

  if (world == nullptr) {
    util::warning(log_tag + "failed to initialized the world");

    return;
  }

  auto* uri = lilv_new_uri(world, plugin_uri.c_str());

  if (uri == nullptr) {
    util::warning(log_tag + "Invalid plugin URI: " + plugin_uri);

    return;
  }

  lilv_world_load_all(world);

  const LilvPlugins* plugins = lilv_world_get_all_plugins(world);

  plugin = lilv_plugins_get_by_uri(plugins, uri);

  lilv_node_free(uri);

  if (plugin == nullptr) {
    util::warning(log_tag + "Could not find the plugin: " + plugin_uri);

    return;
  }

  found_plugin = true;

  check_required_features();

  create_ports();
}

Lv2Wrapper::~Lv2Wrapper() {
  if (instance != nullptr) {
    lilv_instance_deactivate(instance);
    lilv_instance_free(instance);

    instance = nullptr;
  }

  if (world != nullptr) {
    lilv_world_free(world);
  }
}

void Lv2Wrapper::check_required_features() {
  LilvNodes* required_features = lilv_plugin_get_required_features(plugin);

  if (required_features != nullptr) {
    for (auto* i = lilv_nodes_begin(required_features); !lilv_nodes_is_end(required_features, i);
         i = lilv_nodes_next(required_features, i)) {
      const LilvNode* required_feature = lilv_nodes_get(required_features, i);
      const char* required_feature_uri = lilv_node_as_uri(required_feature);

      util::debug(log_tag + plugin_uri + " requires feature: " + required_feature_uri);
    }

    lilv_nodes_free(required_features);
  }
}

void Lv2Wrapper::create_ports() {
  n_ports = lilv_plugin_get_num_ports(plugin);

  ports.resize(n_ports);

  // Get default values for all ports

  std::vector<float> values(n_ports);

  lilv_plugin_get_port_ranges_float(plugin, nullptr, nullptr, values.data());

  LilvNode* lv2_InputPort = lilv_new_uri(world, LV2_CORE__InputPort);
  LilvNode* lv2_OutputPort = lilv_new_uri(world, LV2_CORE__OutputPort);
  LilvNode* lv2_AudioPort = lilv_new_uri(world, LV2_CORE__AudioPort);
  LilvNode* lv2_ControlPort = lilv_new_uri(world, LV2_CORE__ControlPort);
  LilvNode* lv2_connectionOptional = lilv_new_uri(world, LV2_CORE__connectionOptional);

  for (uint n = 0; n < n_ports; n++) {
    auto* port = &ports[n];

    const auto* lilv_port = lilv_plugin_get_port_by_index(plugin, n);

    port->index = n;
    port->name = lilv_node_as_string(lilv_port_get_name(plugin, lilv_port));
    port->symbol = lilv_node_as_string(lilv_port_get_symbol(plugin, lilv_port));
    port->value = std::isnan(values[n]) ? 0.0F : values[n];
    port->optional = lilv_port_has_property(plugin, lilv_port, lv2_connectionOptional);

    // util::warning("port name: " + port.name);
    // util::warning("port symbol: " + port->symbol);

    if (lilv_port_is_a(plugin, lilv_port, lv2_InputPort)) {
      port->is_input = true;
    } else if (!lilv_port_is_a(plugin, lilv_port, lv2_OutputPort) && !port->optional) {
      util::error(log_tag + "Port " + port->name + " is neither input nor output!");
    }

    if (lilv_port_is_a(plugin, lilv_port, lv2_ControlPort)) {
      port->type = TYPE_CONTROL;
    } else if (lilv_port_is_a(plugin, lilv_port, lv2_AudioPort)) {
      port->type = TYPE_AUDIO;

      n_audio_in = (port->is_input) ? n_audio_in + 1 : n_audio_in;
      n_audio_out = (!port->is_input) ? n_audio_out + 1 : n_audio_out;
    } else if (!port->optional) {
      util::error(log_tag + "Port " + port->name + " has un unsupported type!");
    }
  }

  // util::warning("n audio_in ports: " + std::to_string(n_audio_in));
  // util::warning("n audio_out ports: " + std::to_string(n_audio_out));

  lilv_node_free(lv2_connectionOptional);
  lilv_node_free(lv2_ControlPort);
  lilv_node_free(lv2_AudioPort);
  lilv_node_free(lv2_OutputPort);
  lilv_node_free(lv2_InputPort);
}

auto Lv2Wrapper::create_instance(const uint& rate) -> bool {
  if (instance != nullptr) {
    deactivate();
    lilv_instance_free(instance);

    instance = nullptr;
  }

  instance = lilv_plugin_instantiate(plugin, rate, nullptr);

  if (instance == nullptr) {
    util::warning(log_tag + "failed to instantiate " + plugin_uri);

    return false;
  }

  connect_control_ports();

  activate();

  return true;
}

void Lv2Wrapper::connect_control_ports() {
  for (auto& p : ports) {
    if (p.type == PortType::TYPE_CONTROL) {
      lilv_instance_connect_port(instance, p.index, &p.value);
    }
  }
}

void Lv2Wrapper::connect_data_ports(std::vector<float>& left_in,
                                    std::vector<float>& right_in,
                                    std::span<float>& left_out,
                                    std::span<float>& right_out) {
  int count_input = 0;
  int count_output = 0;

  for (auto& p : ports) {
    if (p.type == PortType::TYPE_AUDIO) {
      if (p.is_input) {
        if (count_input == 0) {
          lilv_instance_connect_port(instance, p.index, left_in.data());
        } else if (count_input == 1) {
          lilv_instance_connect_port(instance, p.index, right_in.data());
        }

        count_input++;
      } else {
        if (count_output == 0) {
          lilv_instance_connect_port(instance, p.index, left_out.data());
        } else if (count_output == 1) {
          lilv_instance_connect_port(instance, p.index, right_out.data());
        }

        count_output++;
      }
    }
  }
}

void Lv2Wrapper::set_n_samples(const uint& value) {
  this->n_samples = value;
}

auto Lv2Wrapper::get_n_samples() const -> uint {
  return this->n_samples;
}

void Lv2Wrapper::activate() {
  lilv_instance_activate(instance);
}

void Lv2Wrapper::run() const {
  if (instance != nullptr) {
    lilv_instance_run(instance, n_samples);
  }
}

void Lv2Wrapper::deactivate() {
  lilv_instance_deactivate(instance);
}

}  // namespace lv2