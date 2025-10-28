#include <cstdlib>
#include <cstdint>
#include <math.h>
#include <chrono>
#include <thread>
#include <iostream>
#include "setila_spi.h"
#include "mcp3204.h"

double ntc_temp_c_from_adc(uint16_t adc, uint16_t max_counts, double   r_fixed, bool     ntc_on_top, double   r0, double   beta, double   t0_c);
double measured_temperatures[4];
int measured_adc[4];
void temp_scan(int id){
	int rc;
	SPI_Bus_Master_Device *spi_bus_master = new SPI_Bus_Master_Device("/dev/spidev0.0");

	MCP3204 *ad_MCP3204 = new MCP3204(2.491);

	if (spi_bus_master->open_bus())
	{
		std::cout << "Failed to open bus master device." << std::endl;
		return;
	}

	if (spi_bus_master->configure(SPI_BUS_MODE::MODE_0, MCP3204_SPI_BUS_SPEED, MCP3204_SPI_BITS_PER_WORD, 0))
	{
		std::cout << "Setting SPI bus master parameters failed." << std::endl;
		return;
	}
	
	if (ad_MCP3204->attach_to_bus(spi_bus_master))
	{
		std::cout << "Attach to SPI bus master failed." << std::endl;
		return;
	}
	const int mnum=30;
	int values[4][mnum]={0};
	int measurement=-1;
	while(true){
	measurement++;
	for(int ch=0;ch<4;ch++){
		rc = ad_MCP3204->convert((MCP3204_INPUT_CHANNEL)(ch), MCP3204_INPUT_CHANNEL_MODE::SINGLE_ENDED);

		if (rc)
		{
			std::cout << "MCP3204 Error: communication error while asking for AD conversion. Error code: " << rc << std::endl;
			return;
		}
		values[ch][measurement%mnum] = ad_MCP3204->digital_value();
		int avg = 0;
		for(int z=measurement>=mnum?mnum-1:measurement;z>=0;z--)avg+=values[ch][z];
		avg/=measurement>=mnum?mnum:measurement+1;
		measured_adc[ch]=avg;
		measured_temperatures[ch]=ntc_temp_c_from_adc(avg,4095,ch<2?10000:1600,true,4700,ch<2?3977:4490,25);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	ad_MCP3204->dettach_from_master_bus();

	delete ad_MCP3204;

	spi_bus_master->close_bus();

	delete spi_bus_master;
}
