# MQ-3

MQ-3 standalone acquisition demo.

Layout:

- `mq3_drv/`: kernel misc driver backed by an IIO ADC channel
- `mq3_app/`: userspace raw-value reader
- `mq3_dts_fragment.dts`: example device-tree fragment

Hardware:

- `MQ-3 AO -> ADC1_CH1`
- `MQ-3 GND -> board GND`
- `MQ-3 VCC -> module power input`

The driver expects a device-tree node:

```dts
putemq3 {
	compatible = "pute,putemq3";
	status = "okay";
	io-channels = <&adc1 1>;
	io-channel-names = "mq3";
};
```
