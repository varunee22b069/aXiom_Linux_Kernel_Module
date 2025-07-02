// SPDX-License-Identifier: GPL-2.0
#define DEBUG // Enabled debug messages
#include "axiom_core.h"

#define POLL_INTERVAL_DEFAULT_MS 10

bool poll_enable;
module_param(poll_enable, bool, 0444);
MODULE_PARM_DESC(poll_enable, "Enable polling mode [default 0=no]");

int poll_interval;
module_param(poll_interval, int, 0444);
MODULE_PARM_DESC(poll_interval, "Polling period in ms [default = 100]");

struct axiom_data {
	struct axiom_data_core data_core;
	// I2C client data...
	struct i2c_client *i2cClient;
	bool irq_allocated; // indicates the IRQ was allocated during probe
};

u16 axiom_read_page(void *pAxiomData, u16 target_address, u16 length, u8 *pBuffer)
{
	struct axiom_data *data = pAxiomData;
	struct i2c_client *i2cClient = data->i2cClient;
	struct device *pDev = data->data_core.pDev;
	struct i2c_msg msg[2];
	struct AxiomCmdHeader cmdHeader;
	int ret, i;

	// Build the header
	cmdHeader.target_address = target_address;
	cmdHeader.length = length;
	cmdHeader.read = 1;

	msg[0].addr = i2cClient->addr;
	msg[0].flags = 0; // (odd that the I2C_M_WR flag is not defined in i2c.h)
	msg[0].len = sizeof(cmdHeader);
	msg[0].buf = (u8 *)&cmdHeader;
	msg[1].addr = i2cClient->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = length;
	msg[1].buf = (char *)pBuffer;

	for (i = 0; i < 8; i++) {
		ret = i2c_transfer(i2cClient->adapter, msg, 2);
		if (ret == 2)
			return length;

		dev_err(pDev, "Failed I2C read page. RC:%d\n", ret);
		udelay(data->data_core.bus_holdoff_delay_us);
	}

	udelay(data->data_core.bus_holdoff_delay_us);
	return length;
}
EXPORT_SYMBOL_GPL(axiom_read_page);

u16 axiom_write_page(void *pAxiomData, u16 target_address, u16 length, u8 *pBuffer)
{
	struct axiom_data *data = pAxiomData;
	struct i2c_client *i2cClient = data->i2cClient;
	struct device *pDev = data->data_core.pDev;
	struct i2c_msg msg[1];
	int ret, i;
	u8 write_buf[786];

	write_buf[0] = (target_address & 0x00FF);
	write_buf[1] = (target_address & 0xFF00) >> 8;
	write_buf[2] = (length & 0x00FF);
	write_buf[3] = (length & 0x7F00) >> 8; // Ensure the read bit is clear

	for (i = 0; i < length; i++)
		write_buf[4 + i] = pBuffer[i];

	msg[0].addr = i2cClient->addr;
	msg[0].flags = 0;
	msg[0].len = (4 + length);
	msg[0].buf = (u8 *)&write_buf;

	for (i = 0; i < 8; i++) {
		ret = i2c_transfer(i2cClient->adapter, msg, 1);
		if (ret == 1)
			return length;
		dev_err(pDev, "Failed I2C write page. RC:%d\n", ret);
		udelay(data->data_core.bus_holdoff_delay_us);
	}
	udelay(data->data_core.bus_holdoff_delay_us);
	return length;
}
EXPORT_SYMBOL_GPL(axiom_write_page);

static irqreturn_t axiom_irq(int irq, void *handle)
{
	struct axiom_data *data = handle;
	struct axiom_data_core *data_core = &data->data_core;
	u8 *pRX_data = &data_core->rx_buf[0];

	u16 target_address = usage_to_target_address(data_core, 0x34, 0, 0);

	axiom_read_page(data_core->pAxiomData, target_address, data_core->max_report_len, pRX_data);
	axiom_process_report(&data->data_core, pRX_data);

	return IRQ_HANDLED;
}

static void axiom_i2c_poll(struct input_dev *input_dev)
{
	struct axiom_data_core *data_core = input_get_drvdata(input_dev);
	u8 *pRX_data = &data_core->rx_buf[0];

	u16 target_address = usage_to_target_address(data_core, 0x34, 0, 0);

	axiom_read_page(data_core->pAxiomData, target_address, data_core->max_report_len, pRX_data);

	axiom_process_report(data_core, pRX_data);
}

void axiom_reset(struct axiom_data_core *data_core)
{
	gpiod_set_value_cansleep(data_core->rst_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(data_core->rst_gpio, 0);
	msleep(110);
}
EXPORT_SYMBOL_GPL(axiom_reset);

// purpose: Function called in IRQ context when device is plugged in.
// returns: Error code
#if KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE
static int axiom_i2c_probe(struct i2c_client *i2cClient)
#else
static int axiom_i2c_probe(struct i2c_client *i2cClient, const struct i2c_device_id *id)
#endif
{
#if KERNEL_VERSION(6, 3, 0) <= LINUX_VERSION_CODE
	const struct i2c_device_id *id = i2c_client_get_device_id(i2cClient);
#endif
	struct device *pDev = &i2cClient->dev;
	struct axiom_data *data;
	struct axiom_data_core *data_core;
	u32 startup_delay_ms;
	u32 error = 0;
	u32 target;
	u32 i2cFunctionality;
	bool ret;
	// Kernel will manage this data, it will be automatically unloaded when the
	// module is unloaded.
	dev_info(pDev, "Device address: 0x%04x\n", i2cClient->addr);
	dev_info(pDev, "Device flags: 0x%04x\n", i2cClient->flags);
	dev_info(pDev, "Device name: %s\n", i2cClient->name);
	data = devm_kzalloc(pDev, sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(pDev, "Failed to allocate memory for aXiom data structure!\n");
		return -ENOMEM;
	}
	data_core = &data->data_core;
	data->i2cClient = i2cClient;

	axiom_init_data_core(data_core, pDev, data);
	i2c_set_clientdata(i2cClient, data);

	data_core->rst_gpio = devm_gpiod_get_optional(pDev, "reset", GPIOD_OUT_HIGH);

	if (IS_ERR(data_core->rst_gpio)) {
		dev_err(pDev, "failed to get reset GPIO\n");
		return PTR_ERR(data_core->rst_gpio); // Return the actual error code
	}

	if (data_core->rst_gpio)
		axiom_reset(data_core);

	data_core->vddi = devm_regulator_get_optional(pDev, "VDDI");
	if (!IS_ERR(data_core->vddi)) {
		error = regulator_enable(data_core->vddi);

		if (error) {
			dev_err(&i2cClient->dev, "Failed to enable VDDI regulator\n");
			return error;
		}
	}

	data_core->vdda = devm_regulator_get_optional(pDev, "VDDA");
	if (!IS_ERR(data_core->vdda)) {
		error = regulator_enable(data_core->vdda);
		if (error) {
			dev_err(pDev, "Failed to get VDDA regulator\n");
			regulator_disable(data_core->vddi);
			return error;
		}
		if (!device_property_read_u32(pDev, "startup-time-ms", &startup_delay_ms))
			msleep(startup_delay_ms);
	}

	ret = axiom_discover(data_core);
	if (!ret) {
		dev_info(pDev, "axiom_discover is failed");
		return 0;
	}
	axiom_rebaseline(data_core);

	ret = axiom_dev_init(data_core);
	if (ret)
		dev_info(pDev, "axiom dev node creation is failed");

	if ((i2cClient->irq == 0) &&
		(poll_enable == 0)) {
		dev_err(pDev, "No IRQ specified!\n");
		return -EINVAL;
	}

	i2cFunctionality = i2c_get_functionality(i2cClient->adapter);
	dev_info(pDev, "The i2c adapter reported functionality: 0x%08x\n", i2cFunctionality);

	// Now Register with the Input Sub-System
	//-------------------------------------------------
	data_core->input_dev = axiom_register_input_subsystem(data_core);
	if (data_core->input_dev == NULL) {
		dev_err(pDev, "Failed to register input device, error: %d\n", error);
		return error;
	}
	dev_info(pDev, "AXIOM: I2C driver registered with Input Sub-System.\n");
	//-------------------------------------------------

	// Delay just a smidge before enabling the IRQ
	udelay(data_core->bus_holdoff_delay_us);

	// Ensure that all reports are initialised to not be present.
	for (target = 0; target < U41_MAX_TARGETS; target++)
		data_core->targets[target].state = Target_State_Not_Present;

	data_core->irq_line = i2cClient->irq;
	data->irq_allocated = (0 == (error = devm_request_threaded_irq(pDev, i2cClient->irq,
																   NULL, axiom_irq,
																   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
																   "axiom_irq", data)));
	if (error != 0) {
		dev_warn(pDev, "Request irq failed, falling back to polling mode");

		error = input_setup_polling(data_core->input_dev, axiom_i2c_poll);

		if (error) {
			dev_err(pDev, "Unable to set up polling mode\n");
			return error;
		}

		if (!device_property_read_u32(pDev, "poll-interval", &poll_interval))
			input_set_poll_interval(data_core->input_dev, poll_interval);
		else
			input_set_poll_interval(data_core->input_dev, POLL_INTERVAL_DEFAULT_MS);
	}

	dev_info(pDev, "Reseting the axiom in probe");
	axiom_reset(data_core);
	dev_info(pDev, "Probe End\n");
	return 0;
}

#if (KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE)
static int axiom_i2c_remove(struct i2c_client *i2cClient)
#else
static void axiom_i2c_remove(struct i2c_client *i2cClient)
#endif
{
	struct axiom_data *data;
	struct axiom_data_core *data_core;

	data = i2c_get_clientdata(i2cClient);
	data_core = &data->data_core;

	axiom_dev_exit(data_core);

	if (data->irq_allocated) {
		dev_info(&i2cClient->dev, "freeing IRQ %u...\n", data_core->irq_line);
		devm_free_irq(&i2cClient->dev, data_core->irq_line, data);
		data->irq_allocated = false;
	}

	axiom_remove(data_core);

	dev_info(&i2cClient->dev, "Removed\n");
#if (KERNEL_VERSION(6, 1, 0) > LINUX_VERSION_CODE)
	return 0;
#endif
}
static const struct i2c_device_id axiom_i2c_id_table[] = {
	{"axiom"},
	{},
};
MODULE_DEVICE_TABLE(i2c, axiom_i2c_id_table);

static const struct of_device_id axiom_i2c_dt_ids[] = {
	{
		.compatible = "axiom_i2c_a,ax310",
		.data = "ax310",
	},
	{}};
MODULE_DEVICE_TABLE(of, axiom_i2c_dt_ids);

static struct i2c_driver axiom_i2c_driver = {
	.driver = {
		.name = "axiom_i2c",
		.of_match_table = of_match_ptr(axiom_i2c_dt_ids),
	},
	.id_table = axiom_i2c_id_table,
	.probe = axiom_i2c_probe,
	.remove = axiom_i2c_remove,
};

module_i2c_driver(axiom_i2c_driver);

MODULE_DESCRIPTION("aXiom touchscreen I2C bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("i2c:axiom");
