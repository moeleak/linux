// SPDX-License-Identifier: GPL-2.0
/* T-HEAD TH1520 AON firmware regulator driver */

#include <linux/auxiliary_bus.h>
#include <asm/byteorder.h>
#include <linux/firmware/thead/thead,th1520-aon.h>
#include <linux/module.h>
#include <linux/regulator/driver.h>
#include <linux/slab.h>

#define TH1520_AON_APCPU_DVDD_DVDDM 3

struct th1520_aon_regulator_msg {
	struct th1520_aon_rpc_msg_hdr hdr;
	__be16 regulator_id;
	__be16 dual_rail;
	__be32 voltage0;
	__be32 voltage1;
	__be16 reserved[6];
} __packed __aligned(1);

struct th1520_aon_cpu_voltage {
	u32 vdd;
	u32 vddm;
};

struct th1520_aon_regulator {
	struct th1520_aon_chan *aon_chan;
	const struct th1520_aon_cpu_voltage *cpu_voltage;
};

static const struct th1520_aon_cpu_voltage th1520_aon_cpu_voltages[] = {
	{ 600000, 750000 },
	{ 600000, 800000 },
	{ 650000, 800000 },
	{ 720000, 770000 },
	{ 700000, 800000 },
	{ 720000, 820000 },
	{ 800000, 800000 },
	{ 820000, 820000 },
	{ 1000000, 1000000 },
};

static const struct th1520_aon_cpu_voltage *
th1520_aon_find_cpu_voltage(u32 vdd)
{
	const struct th1520_aon_cpu_voltage *matched = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(th1520_aon_cpu_voltages); i++) {
		const struct th1520_aon_cpu_voltage *cpu_voltage;

		cpu_voltage = &th1520_aon_cpu_voltages[i];
		if (cpu_voltage->vdd == vdd)
			matched = cpu_voltage;
	}

	return matched;
}

static int th1520_aon_set_cpu_voltage(struct th1520_aon_regulator *regulator,
				      u32 vdd, u32 vddm)
{
	struct th1520_aon_regulator_msg msg = {};
	struct th1520_aon_rpc_msg_hdr *hdr = &msg.hdr;

	hdr->svc = TH1520_AON_RPC_SVC_PM;
	hdr->func = TH1520_AON_PM_FUNC_SET_RESOURCE_REGULATOR;
	hdr->size = TH1520_AON_RPC_MSG_NUM;

	msg.regulator_id = cpu_to_be16(TH1520_AON_APCPU_DVDD_DVDDM);
	msg.dual_rail = cpu_to_be16(1);
	msg.voltage0 = cpu_to_be32(vdd);
	msg.voltage1 = cpu_to_be32(vddm);

	return th1520_aon_call_rpc(regulator->aon_chan, &msg);
}

static int th1520_aon_cpu_set_voltage(struct regulator_dev *rdev,
				      int min_uV, int max_uV,
				      unsigned int *selector)
{
	struct th1520_aon_regulator *regulator = rdev_get_drvdata(rdev);
	const struct th1520_aon_cpu_voltage *cpu_voltage;
	int ret;

	cpu_voltage = th1520_aon_find_cpu_voltage(min_uV);
	if (!cpu_voltage || cpu_voltage->vdd > max_uV)
		return -EINVAL;

	ret = th1520_aon_set_cpu_voltage(regulator, cpu_voltage->vdd,
					       cpu_voltage->vddm);
	if (ret)
		return ret;

	regulator->cpu_voltage = cpu_voltage;
	*selector = cpu_voltage - th1520_aon_cpu_voltages;

	return 0;
}

static int th1520_aon_cpu_list_voltage(struct regulator_dev *rdev,
					      unsigned int selector)
{
	if (selector >= ARRAY_SIZE(th1520_aon_cpu_voltages))
		return -EINVAL;

	return th1520_aon_cpu_voltages[selector].vdd;
}

static int th1520_aon_cpu_get_voltage(struct regulator_dev *rdev)
{
	struct th1520_aon_regulator *regulator = rdev_get_drvdata(rdev);

	return regulator->cpu_voltage->vdd;
}

static const struct regulator_ops th1520_aon_cpu_ops = {
	.list_voltage = th1520_aon_cpu_list_voltage,
	.set_voltage = th1520_aon_cpu_set_voltage,
	.get_voltage = th1520_aon_cpu_get_voltage,
};

static const struct regulator_desc th1520_aon_cpu_desc = {
	.name = "APCPU_DVDD",
	.of_match = "appcpu_dvdd",
	.regulators_node = "regulators",
	.ops = &th1520_aon_cpu_ops,
	.n_voltages = ARRAY_SIZE(th1520_aon_cpu_voltages),
	.type = REGULATOR_VOLTAGE,
	.owner = THIS_MODULE,
};

static int th1520_aon_regulator_probe(struct auxiliary_device *adev,
				      const struct auxiliary_device_id *id)
{
	struct regulator_config config = {};
	struct th1520_aon_regulator *regulator;
	struct regulator_dev *rdev;

	regulator = devm_kzalloc(&adev->dev, sizeof(*regulator), GFP_KERNEL);
	if (!regulator)
		return -ENOMEM;

	regulator->aon_chan = adev->dev.platform_data;
	if (!regulator->aon_chan)
		return -ENODEV;

	regulator->cpu_voltage = th1520_aon_find_cpu_voltage(650000);
	if (!regulator->cpu_voltage)
		return -EINVAL;

	config.dev = adev->dev.parent;
	config.driver_data = regulator;

	rdev = devm_regulator_register(&adev->dev, &th1520_aon_cpu_desc,
					 &config);
	if (IS_ERR(rdev))
		return dev_err_probe(&adev->dev, PTR_ERR(rdev),
				     "failed to register CPU regulator\n");

	auxiliary_set_drvdata(adev, regulator);

	return 0;
}

static const struct auxiliary_device_id th1520_aon_regulator_id_table[] = {
	{ .name = "th1520_pm_domains.regulator" },
	{ }
};
MODULE_DEVICE_TABLE(auxiliary, th1520_aon_regulator_id_table);

static struct auxiliary_driver th1520_aon_regulator_driver = {
	.probe = th1520_aon_regulator_probe,
	.id_table = th1520_aon_regulator_id_table,
};
module_auxiliary_driver(th1520_aon_regulator_driver);

MODULE_DESCRIPTION("T-HEAD TH1520 AON firmware regulator driver");
MODULE_LICENSE("GPL");
