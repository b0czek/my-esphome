#pragma once

#include "esphome.h"
#include "lvgl.h"
#include <string>

#define CLIMATE_DISPLAY_ARC_MIN_VAL 30
#define CLIMATE_DISPLAY_ARC_MAX_VAL 80
#define CLIMATE_DISPLAY_TEXT_PLACEHOLDER "..."
#define CLIMATE_DISPLAY_COMPONENT_NAME "ClimateDisplay"

class ClimateDisplay : public Component, public CustomAPIDevice
{

public:
    ClimateDisplay(std::string climateEntityId, std::string fanEntityId)
    {
        climate_entity_id = climateEntityId;
        fan_entity_id = fanEntityId;
    }

    void setup() override
    {
        arc = lv_arc_create(lv_scr_act());
        lv_obj_set_size(arc, 160, 160);
        lv_arc_set_rotation(arc, 135);
        lv_arc_set_range(arc, CLIMATE_DISPLAY_ARC_MIN_VAL, CLIMATE_DISPLAY_ARC_MAX_VAL);
        lv_arc_set_bg_angles(arc, 0, 270);
        lv_arc_set_value(arc, CLIMATE_DISPLAY_ARC_MIN_VAL);
        lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_KNOB);
        lv_obj_set_style_arc_width(arc, 5, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, 5, LV_PART_INDICATOR);

        lv_obj_set_flex_flow(arc, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(arc, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_SPACE_EVENLY);

        current_label = lv_label_create(arc);
        lv_label_set_text(current_label, CLIMATE_DISPLAY_TEXT_PLACEHOLDER);
        lv_obj_set_style_text_font(current_label, &lv_font_montserrat_40, 0);

        target_label = lv_label_create(arc);
        lv_label_set_text(target_label, CLIMATE_DISPLAY_TEXT_PLACEHOLDER);
        lv_obj_set_style_text_font(target_label, &lv_font_montserrat_22, 0);

        action_label = lv_label_create(arc);
        lv_label_set_text(action_label, CLIMATE_DISPLAY_TEXT_PLACEHOLDER);
        lv_obj_set_style_text_font(action_label, &lv_font_montserrat_18, 0);

        subscribe_homeassistant_state(&ClimateDisplay::on_temp_changed, climate_entity_id, "temperature");
        subscribe_homeassistant_state(&ClimateDisplay::on_state_changed, climate_entity_id);
        subscribe_homeassistant_state(&ClimateDisplay::on_current_temp_changed, climate_entity_id, "current_temperature");
        subscribe_homeassistant_state(&ClimateDisplay::on_hvac_action_changed, climate_entity_id, "hvac_action");
        subscribe_homeassistant_state(&ClimateDisplay::on_speed_changed, fan_entity_id);
    }

    void modify_target_temp(int change)
    {
        int new_temp = lv_arc_get_value(arc) + change;
        std::string temp = std::to_string(new_temp);
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "changing climate temp to: %s", temp.c_str());
        call_homeassistant_service("climate.set_temperature", {
                                                                  {"entity_id", climate_entity_id},
                                                                  {"temperature", temp},
                                                              });
    }
    void toggle_state()
    {
        std::string new_state = state == "off" ? "climate.turn_on" : "climate.turn_off";
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "changing climate state to %s", new_state.c_str());
        call_homeassistant_service(new_state, {
                                                  {"entity_id", climate_entity_id},
                                              });
    }

    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

private:
    lv_obj_t *target_label;
    lv_obj_t *current_label;
    lv_obj_t *arc;
    lv_obj_t *action_label;

    std::string state;
    std::string speed;
    std::string action;
    std::string climate_entity_id;
    std::string fan_entity_id;

    void on_state_changed(std::string s)
    {
        state = s;
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "climate changed state to %s", state.c_str());
        if (s == "heat")
        {
            lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(arc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_KNOB);
        }
        else
        {
            lv_obj_set_style_arc_color(arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(arc, lv_palette_main(LV_PALETTE_GREY), LV_PART_KNOB);
        }
    }

    void on_speed_changed(std::string state)
    {
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "speed changed to %s", state.c_str());
        speed = state;
        update_action_label();
    }

    void on_hvac_action_changed(std::string state)
    {
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "climate action changed to %s", state.c_str());
        action = state;
        update_action_label();
    }

    void update_action_label()
    {
        if (action == "heating")
        {
            lv_label_set_text(action_label, ("On - " + speed + "%").c_str());
        }
        else if (action == "idle")
        {
            lv_label_set_text(action_label, "Idle");
        }
        else
        {
            lv_label_set_text(action_label, "Off");
        }
    }

    void on_current_temp_changed(std::string state)
    {
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "current temperature changed to %s", state.c_str());

        lv_label_set_text(current_label, (state + "°C").c_str());
    }

    void on_temp_changed(std::string state)
    {
        int value = std::stoi(state);
        ESP_LOGD(CLIMATE_DISPLAY_COMPONENT_NAME, "target temperature changed to %d", value);
        lv_label_set_text(target_label, (state + "°C").c_str());
        lv_arc_set_value(arc, value);
    }
};
