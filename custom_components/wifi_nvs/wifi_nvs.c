
#include "wifi_nvs.h"

esp_err_t wifi_manager_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS needs to be erased
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t wifi_manager_nvs_save(nvs_wifi_info_t *info)
{
    nvs_handle_t handle;
    if (nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK)
    {
        nvs_set_str(handle, "ssid", info->ssid);
        nvs_set_str(handle, "password", info->password);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(WIFI_NVS_TAG, "Saved WiFi credentials to NVS%s, %s", info->ssid, info->password);
    }
    return ESP_OK;
}

esp_err_t wifi_manager_nvs_get(nvs_wifi_info_t *info)
{
    if (!info)
        return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    size_t ssid_len = sizeof(info->ssid);
    size_t pass_len = sizeof(info->password);
    err = nvs_get_str(handle, "ssid", info->ssid, &ssid_len);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return ESP_FAIL;
    }
    err = nvs_get_str(handle, "password", info->password, &pass_len);
    if (err != ESP_OK)
    {
        nvs_close(handle);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t wifi_manager_nvs_clear(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        return err;
    }

    // 删除 key
    nvs_erase_key(handle, "ssid");
    nvs_erase_key(handle, "password");

    // 提交更改
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI("wifi_nvs", "WiFi credentials cleared from NVS");
    return ESP_OK;
}
