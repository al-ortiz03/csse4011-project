#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#define CUSTOM_SERVICE_UUID BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define CUSTOM_CHAR_UUID BT_UUID_128_ENCODE(0x87654321, 0x4321, 0x8765, 0x4321, 0x56789abcdef0)

static struct bt_uuid_128 custom_service_uuid = BT_UUID_INIT_128(CUSTOM_SERVICE_UUID);
static struct bt_uuid_128 custom_char_uuid = BT_UUID_INIT_128(CUSTOM_CHAR_UUID);

static struct bt_conn *current_conn = NULL;
static uint32_t expected_packet_count = 0;

static uint8_t custom_value[512] = {0};
static uint16_t custom_value_len = 0;

static uint8_t check_defective(uint8_t length_raw, uint8_t width_raw)
{
    uint8_t actual_length = 51 - length_raw ;
    uint8_t actual_width = 51 - width_raw;
    
    bool is_product1_length = (actual_length >= 12 && actual_length <= 16); 
    bool is_product1_width = (actual_width <= 23 && actual_width >= 19);
    
    
    if (is_product1_length && is_product1_width) {
        return 0; // Not defective
    }
    
    return 1;
}

static ssize_t on_receive(struct bt_conn *conn,
                         const struct bt_gatt_attr *attr,
                         const void *buf,
                         uint16_t len,
                         uint16_t offset,
                         uint8_t flags)
{
    if (!conn || !attr || !buf) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_HANDLE);
    }

    if (offset + len > sizeof(custom_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (conn != current_conn) {
        //printk("Warning: Data received from non-current connection\n");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_HANDLE);
    }

    memcpy(custom_value + offset, buf, len);
    custom_value_len = offset + len;

    if (len >= 5) {
        uint8_t packet_count = ((uint8_t *)buf)[4];
        if (packet_count != (expected_packet_count & 0xFF)) {
            // printk("Warning: Packet sequence mismatch. Expected: %u, Got: %u\n",
            // expected_packet_count & 0xFF, packet_count);
        }
        expected_packet_count++;
    }

    //printk("Received data, length: %u\n", len);
    //printk("Data: \n");
    uint8_t length = ((uint8_t*)buf)[0];
    uint8_t width = ((uint8_t*)buf)[1]; 
    uint8_t mass = ((uint8_t*)buf)[2];
    uint8_t color = 0XFFFF;

    uint8_t defective = check_defective(length, width);

    printf("{\"length\": %u, \"width\": %u, \"mass\": %u, \"color\": %u, \"defective\": %u}\n", 
           51- length, 51 - width, mass, color, defective);
    //printk("\n");

    return len;
}

BT_GATT_SERVICE_DEFINE(custom_svc,
    BT_GATT_PRIMARY_SERVICE(&custom_service_uuid),
    BT_GATT_CHARACTERISTIC(&custom_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_WRITE,
                          NULL, on_receive, NULL),
);

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (err) {
        printk("Connection failed (err %u)\n", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Connected to %s\n", addr);

    if (current_conn) {
        bt_conn_unref(current_conn);
    }
    current_conn = bt_conn_ref(conn);
    expected_packet_count = 0;

    k_sleep(K_MSEC(100));

    struct bt_le_conn_param conn_param = {
        .interval_min = 0x0050, /* 80ms */
        .interval_max = 0x0070, /* 112ms */
        .latency = 0,
        .timeout = 400, /* 4s */
    };

    err = bt_conn_le_param_update(conn, &conn_param);
    if (err) {
        printk("Failed to update connection parameters (err %d)\n", err);
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("Disconnected from %s (reason %u)\n", addr, reason);

    if (current_conn) {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    custom_value_len = 0;
    expected_packet_count = 0;
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int main(void)
{
    int err;

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return err;
    }

    k_sleep(K_MSEC(500));

    printk("Bluetooth initialized\n");

    struct bt_le_adv_param adv_param = BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
        0x0640, /* 1000ms */
        0x0640, /* 1000ms */
        NULL);

    struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA_BYTES(BT_DATA_UUID128_ALL, CUSTOM_SERVICE_UUID),
    };

    k_sleep(K_MSEC(100));

    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return err;
    }

    printk("Advertising started\n");
    printk("Waiting for connections...\n");

    return 0;
}
