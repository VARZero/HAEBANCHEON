// SPDX-License-Identifier: GPL-2.0
/* Existing 100MHz, polling-only peripherals. No FPGA changes or invented IRQs. */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/serial_8250.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/property.h>

struct hb_bus {void __iomem *base;u32 clock;int uart_line;};
static int hb_done(struct hb_bus *b, u32 *status)
{
 return readl_poll_timeout(b->base+4,*status,*status&BIT(1),10,50000);
}
static void hb_spi_cs(struct spi_device *spi,bool inactive)
{
 struct hb_bus *b=spi_controller_get_devdata(spi->controller);
 u32 cfg=readl(b->base+12)&3;
 writel(cfg|(inactive?0:BIT(2)),b->base+12);
}
static int hb_spi_prepare(struct spi_controller *ctlr,struct spi_message *msg)
{
 struct hb_bus *b=spi_controller_get_devdata(ctlr);
 writel((readl(b->base+12)&BIT(2))|(msg->spi->mode&SPI_CPOL?1:0)|(msg->spi->mode&SPI_CPHA?2:0),b->base+12);
 return 0;
}
static int hb_spi_transfer(struct spi_controller *ctlr,struct spi_device *spi,struct spi_transfer *t)
{
 struct hb_bus *b=spi_controller_get_devdata(ctlr);const u8 *tx=t->tx_buf;u8 *rx=t->rx_buf;
 u32 status,hz=t->speed_hz?:spi->max_speed_hz;unsigned int i;int rc;
 if(!hz||(t->bits_per_word&&t->bits_per_word!=8))return -EINVAL;
 writel(clamp_t(u32,DIV_ROUND_UP(b->clock,2*hz),8,65535),b->base+8);
 for(i=0;i<t->len;i++){
  writel(tx?tx[i]:0,b->base);writel(1,b->base+16);
  rc=hb_done(b,&status);if(rc)return rc;if(rx)rx[i]=readl(b->base)&255;
 }return 0;
}
static int hb_i2c_command(struct hb_bus *b,u32 command)
{
 u32 status;int rc;writel(BIT(5),b->base+16);writel(command,b->base+16);
 rc=hb_done(b,&status);if(rc)return rc;
 if(status&BIT(3))return -ETIMEDOUT;
 if(status&BIT(4))return -EAGAIN;
 if(status&BIT(2))return -EREMOTEIO;
 return 0;
}
static int hb_i2c_xfer(struct i2c_adapter *adapter,struct i2c_msg *msgs,int count)
{
 struct hb_bus *b=i2c_get_adapdata(adapter);int n,j,rc=0;
 for(n=0;n<count;n++)if(msgs[n].flags&~(I2C_M_RD|I2C_M_DMA_SAFE))return -EOPNOTSUPP;
 for(n=0;n<count;n++){
  struct i2c_msg *m=&msgs[n];bool last=n==count-1;
  writel((m->addr<<1)|!!(m->flags&I2C_M_RD),b->base);
  rc=hb_i2c_command(b,BIT(0)|BIT(3)|(!m->len&&last?BIT(1):0));
  if(rc){if(rc==-EREMOTEIO)rc=-ENXIO;goto failed;}
  for(j=0;j<m->len;j++){
   bool end=j==m->len-1;u32 cmd;
   if(m->flags&I2C_M_RD)cmd=BIT(2)|(end?BIT(4):0);
   else {writel(m->buf[j],b->base);cmd=BIT(3);}
   if(last&&end)cmd|=BIT(1);
   rc=hb_i2c_command(b,cmd);if(rc)goto failed;
   if(m->flags&I2C_M_RD)m->buf[j]=readl(b->base)&255;
  }
 }return count;
failed:
 if(readl(b->base+4)&BIT(5))hb_i2c_command(b,BIT(1));
 return rc;
}
static u32 hb_i2c_functionality(struct i2c_adapter *a){return I2C_FUNC_I2C|I2C_FUNC_SMBUS_EMUL;}
static const struct i2c_algorithm hb_i2c_algo={.master_xfer=hb_i2c_xfer,.functionality=hb_i2c_functionality};
static int hb_probe(struct platform_device *pdev)
{
 struct device *dev=&pdev->dev;struct hb_bus *b;struct resource *res;unsigned long kind=(unsigned long)of_device_get_match_data(dev);int rc;
 b=devm_kzalloc(dev,sizeof(*b),GFP_KERNEL);if(!b)return -ENOMEM;
 b->clock=100000000;device_property_read_u32(dev,"clock-frequency",&b->clock);platform_set_drvdata(pdev,b);
 if(kind==1){
  struct uart_8250_port port={};res=platform_get_resource(pdev,IORESOURCE_MEM,0);if(!res)return -EINVAL;
  port.port.dev=dev;port.port.mapbase=res->start;port.port.mapsize=resource_size(res);port.port.uartclk=b->clock;
  port.port.iotype=UPIO_MEM;port.port.type=PORT_8250;port.port.fifosize=1;
  port.port.flags=UPF_FIXED_TYPE|UPF_SKIP_TEST|UPF_IOREMAP;
  port.port.quirks=UPQ_NO_TXEN_TEST;
  port.port.irq=0; /* serial8250's no-IRQ timer drains the existing RX queue. */
  rc=serial8250_register_8250_port(&port);if(rc<0)return rc;b->uart_line=rc;return 0;
 }
 b->base=devm_platform_ioremap_resource(pdev,0);if(IS_ERR(b->base))return PTR_ERR(b->base);
 if(kind==2){
  struct spi_controller *ctlr=devm_spi_alloc_host(dev,sizeof(*b));if(!ctlr)return -ENOMEM;
  *(struct hb_bus *)spi_controller_get_devdata(ctlr)=*b;ctlr->dev.of_node=dev->of_node;ctlr->num_chipselect=1;ctlr->bus_num=of_alias_get_id(dev->of_node,"spi");
  ctlr->mode_bits=SPI_CPOL|SPI_CPHA;ctlr->bits_per_word_mask=SPI_BPW_MASK(8);
  ctlr->min_speed_hz=DIV_ROUND_UP(b->clock,2*65535);ctlr->max_speed_hz=b->clock/16;
  ctlr->prepare_message=hb_spi_prepare;ctlr->set_cs=hb_spi_cs;ctlr->transfer_one=hb_spi_transfer;
  return devm_spi_register_controller(dev,ctlr);
 }
 if(kind==3){
  struct i2c_adapter *a=devm_kzalloc(dev,sizeof(*a),GFP_KERNEL);u32 speed=100000;if(!a)return -ENOMEM;
  device_property_read_u32(dev,"bus-frequency",&speed);if(!speed||speed>400000)return -EINVAL;
  writel(clamp_t(u32,DIV_ROUND_UP(b->clock,2*speed),8,65535),b->base+8);writel(b->clock/100,b->base+12);
  a->owner=THIS_MODULE;a->algo=&hb_i2c_algo;a->dev.parent=dev;a->dev.of_node=dev->of_node;strscpy(a->name,"HAEBANCHEON I2C",sizeof(a->name));i2c_set_adapdata(a,b);
  return devm_i2c_add_adapter(dev,a);
 }return -ENODEV;
}
static void hb_remove(struct platform_device *pdev)
{
 if((unsigned long)of_device_get_match_data(&pdev->dev)==1)serial8250_unregister_port(((struct hb_bus *)platform_get_drvdata(pdev))->uart_line);
}
static const struct of_device_id hb_match[]={
 {.compatible="oryukdo,uart-poll-v1",.data=(void *)1},{.compatible="oryukdo,spi-v1",.data=(void *)2},{.compatible="oryukdo,i2c-v1",.data=(void *)3},{}
};
MODULE_DEVICE_TABLE(of,hb_match);
static struct platform_driver hb_driver={.probe=hb_probe,.remove=hb_remove,.driver={.name="haebancheon-peripherals",.of_match_table=hb_match}};
module_platform_driver(hb_driver);
MODULE_LICENSE("GPL");
