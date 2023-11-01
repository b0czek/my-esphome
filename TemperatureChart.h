#include "lvgl.h"
#include "esphome.h"

#define TEMPERATURE_CHART_X_AXIS_TICKS 3
#define TEMPERATURE_CHART_DEFAULT_REFRESH_TIME_MS 60000
#define TEMPERATURE_CHART_COMPONENT_NAME "TemperatureChart"
#define TEMPERATURE_CHART_MIN_VALUE 20
#define TEMPERATURE_CHART_MAX_VALUE 80

static int32_t get_tick_time(int32_t max_sample_age, int32_t value)
{

    int32_t seconds_between_ticks = max_sample_age / (TEMPERATURE_CHART_X_AXIS_TICKS - 1);
    // invert the values, because they are numbered from left to right, starting with 0
    int32_t inverted_value = TEMPERATURE_CHART_X_AXIS_TICKS - (value + 1);

    return inverted_value * seconds_between_ticks;
}

// this isn't even used
static void chart_draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (!lv_obj_draw_part_check_type(dsc, &lv_chart_class, LV_CHART_DRAW_PART_TICK_LABEL))
        return;
    if (dsc->id == LV_CHART_AXIS_PRIMARY_X && dsc->text)
    {
        // cast from user data
        int32_t max_sample_age = (*(int32_t *)(e->user_data)) * 3600;

        int32_t tick_time = get_tick_time(max_sample_age, dsc->value);
        float tick_value = (float)tick_time / (tick_time < 3600 ? 60.0 : 3600.0);

        // select formatter depending on whether the time is bigger than hour and if it has a decimal place
        const char *formatter = tick_time < 3600 ? "%.0fm" : tick_value == (int)tick_value ? "%.0fh"
                                                                                           : "%.1fh";
        lv_snprintf(dsc->text, dsc->text_length, formatter, tick_value);
    }
}

class TemperatureChart : public PollingComponent
{

public:
    TemperatureChart(
        int32_t hoursBack,
        std::string sensorEntityName,
        std::string authToken,
        std::string callbackIp,
        std::string callbackPort) : PollingComponent(TEMPERATURE_CHART_DEFAULT_REFRESH_TIME_MS)
    {

        hours_back = hoursBack;
        sensor_entity_name = sensorEntityName;
        time_synced = false;
        auth_token = authToken;
        callback_ip = callbackIp;
        callback_port = callbackPort;
        values = (int16_t *)calloc(sizeof(int16_t), hours_back * 60);
    }

    // std bind probably must not be in constructor
    void current_timestamp()
    {
        id(time_sync).add_on_time_sync_callback(std::bind(&TemperatureChart::on_time_sync, this));
    }

    void setup() override
    {
        chart = lv_chart_create(lv_scr_act());
        lv_obj_set_size(chart, 220, 140);
        lv_chart_set_type(chart, LV_CHART_TYPE_LINE);

        lv_obj_set_style_line_width(chart, 5, LV_PART_ITEMS);
        lv_obj_set_style_size(chart, 5, LV_PART_INDICATOR);

        lv_chart_set_point_count(chart, hours_back * 60);
        lv_chart_set_div_line_count(chart, 5, 5);

        series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_ORANGE), LV_CHART_AXIS_PRIMARY_Y);
        // all values are multiplied by 10 to be stored as ints but preserve .1 decimal accuracy
        lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, TEMPERATURE_CHART_MIN_VALUE * 10, TEMPERATURE_CHART_MAX_VALUE * 10);

        lv_chart_set_ext_y_array(chart, series, values);

        current_timestamp();
    }

    void update() override
    {
        ESP_LOGD(TEMPERATURE_CHART_COMPONENT_NAME, "triggered update, free heap:     %d", ESP.getFreeHeap());
        refresh_chart();
    }
    float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

private:
    std::string sensor_entity_name;
    int32_t hours_back;

    std::string auth_token;
    std::string callback_ip;
    std::string callback_port;

    boolean time_synced;

    int16_t *values;
    lv_obj_t *chart;
    lv_chart_series_t *series;

    JsonArray fetch_api(std::string since)
    {
        ESP_LOGD(TEMPERATURE_CHART_COMPONENT_NAME, "fetching api started");

        HTTPClient http;
        http.begin(("http://" + callback_ip + ":" + callback_port + "/api/history/period/" + since + "?filter_entity_id=" + sensor_entity_name + "&minimal_response").c_str());
        http.addHeader("Authorization", ("Bearer " + auth_token).c_str());
        http.addHeader("Content-Type", "application/json");
        http.useHTTP10(true);
        http.GET();

        DynamicJsonDocument doc(65536);
        DeserializationError err = deserializeJson(doc, http.getStream());
        if (err)
        {
            ESP_LOGE(TEMPERATURE_CHART_COMPONENT_NAME, "error parsing json: %s", err.c_str());
        }
        if (doc.size() == 0)
        {
            return doc.to<JsonArray>();
        }
        else
        {
            return doc[0];
        }
    }

    size_t datetime_parse(const char *dateString)
    {
        const char *format = "%d-%d-%dT%d:%d:%d.%d+00:00";
        struct tm t
        {
        };
        int msec;
        int parseCount = sscanf(dateString, format, &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec, &msec);
        if (parseCount < 7)
        {
            return 0;
        }

        t.tm_year -= 1900;
        t.tm_mon -= 1;

        ESPTime time = ESPTime::from_c_tm(&t, 0);
        time.recalc_timestamp_utc(false);

        return time.timestamp;
    }

    void update_array(JsonArray root)
    {
        ESP_LOGD(TEMPERATURE_CHART_COMPONENT_NAME, "received json array with %d records", root.size());

        int length = hours_back * 60;
        ESPTime time = id(time_sync).utcnow();

        // there might be more data points in received json than there is space in data array
        // so for each point on chart value closest in time to it must be chosen

        // start iterating data from the end
        int json_idx = root.size() - 1;

        for (int i = length - 1; i >= 0; i--)
        {
            // looking for value closest in time
            values[i] = INT16_MAX;
            int32_t lowest_timestamp_diff = 3600 * hours_back;
            int closest_idx = json_idx;

            int j = json_idx;
            while (j >= 0)
            {
                const char *last_changed = root[j]["last_changed"];
                int32_t timestamp = datetime_parse(last_changed);

                int32_t timestamp_diff = std::abs(time.timestamp - timestamp);
                // abs (dont know why it's done twice, probably std::abs didnt work??)
                if (timestamp_diff < 0)
                {
                    timestamp_diff *= -1;
                }
                // value closer in time found
                if (timestamp_diff < lowest_timestamp_diff)
                {
                    lowest_timestamp_diff = timestamp_diff;
                    closest_idx = j;
                }
                // value closest was already found
                else
                {

                    break;
                }
                j--;
            }

            time = ESPTime::from_epoch_utc(time.timestamp - 60);
            json_idx = j;

            // if data is invalid
            const char *state_str = root[closest_idx]["state"];
            if (root[closest_idx]["state"] == nullptr || strcmp(state_str, "invalid") == 0 || strcmp(state_str, "unknown") == 0)
            {
                continue;
            }

            values[i] = (int)(std::atof(state_str) * 10);
        }
    }

    void refresh_chart()
    {
        if (!time_synced)
        {
            return;
        }

        ESPTime since_time = ESPTime::from_epoch_utc(id(time_sync).timestamp_now() - hours_back * 3600);
        std::string since = since_time.strftime("%FT%TZ");
        JsonArray root = fetch_api(since);

        update_array(root);
        lv_chart_refresh(chart);
    }

    void on_time_sync()
    {
        ESP_LOGD(TEMPERATURE_CHART_COMPONENT_NAME, "time synchronized, timestamp is: %d", id(time_sync).timestamp_now());

        time_synced = true;

        refresh_chart();
    }
};
