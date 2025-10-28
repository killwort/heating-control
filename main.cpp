#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <gpiod.hpp>
#include <iostream>
#include <thread>
#include <microhttpd.h>
#include <string>

extern double measured_temperatures[4];
extern int measured_adc[4];

void temp_scan(int id);

const ::std::filesystem::path chip_path("/dev/gpiochip0");
const int line_offset_1=199;
const int line_offset_2=198;
const int line_offset_3=201;
int switch_address[3]={199,198,201};
bool switch_enabled[3]={false};

void set_switch(char* arg, int channel, bool value){
	auto chip=::gpiod::chip(chip_path);
	auto line=chip.get_line(switch_address[channel]);
	auto req=::gpiod::line_request{arg,::gpiod::line_request::DIRECTION_OUTPUT,0};
	line.request(req);
	line.set_direction_output();
	line.set_value(value?1:0);
	switch_enabled[channel]=value;
}

MHD_Result answer(void *cls, struct MHD_Connection *connection,
           const char *url, const char *method,
           const char *ver, const char *upload_data,
           size_t *upload_data_size, void **con_cls)
{
	std::stringstream response;
	if(strncmp(url, "/metrics", 8)==0){
		response<<"measured_temp{place=\"floor1Return\"} "<<measured_temperatures[0]<<std::endl;
		response<<"measured_temp{place=\"floor2Return\"} "<<measured_temperatures[1]<<std::endl;
		response<<"measured_temp{place=\"boiler\"} "<<measured_temperatures[2]<<std::endl;
		response<<"measured_temp{place=\"solarCollector\"} "<<measured_temperatures[3]<<std::endl;
		response<<"measured_adc{place=\"floor1Return\"} "<<measured_adc[0]<<std::endl;
		response<<"measured_adc{place=\"floor2Return\"} "<<measured_adc[1]<<std::endl;
		response<<"measured_adc{place=\"boiler\"} "<<measured_adc[2]<<std::endl;
		response<<"measured_adc{place=\"solarCollector\"} "<<measured_adc[3]<<std::endl;
		response<<"pump_state{place=\"floor1\"} "<<(switch_enabled[0]?1:0)<<std::endl;
		response<<"pump_state{place=\"floor2\"} "<<(switch_enabled[1]?1:0)<<std::endl;
		response<<"pump_state{place=\"solarCollector\"} "<<(switch_enabled[2]?1:0)<<std::endl;
	}else{
		response<<"{\"measured\":{\"floor1ReturnTemp\":";
		response<<measured_temperatures[0]<<",\"floor2ReturnTemp\":"<<measured_temperatures[1];
		response<<",\"boilerTemp\":"<<measured_temperatures[2]<<",\"solarCollectorTemp\":"<<measured_temperatures[3];
		response<<"},\"control\":{\"solarPumpEnabled\":"<<(switch_enabled[2]?"true":"false");
		response<<",\"floor1PumpEnabled\":"<<(switch_enabled[0]?"true":"false");
		response<<",\"floor2PumpEnabled\":"<<(switch_enabled[1]?"true":"false");
		response<<"}}";
	}
    auto str=response.str();
    struct MHD_Response *resp = MHD_create_response_from_buffer(str.size(),
                              (void*)str.c_str(), MHD_RESPMEM_MUST_COPY);
	if(strncmp(url, "/metrics", 8)==0)
		MHD_add_response_header(resp, "Content-Type", "text/plain");
	else
		MHD_add_response_header(resp, "Content-Type", "application/json");
    auto ret = MHD_queue_response(connection, MHD_HTTP_OK, resp);
    MHD_destroy_response(resp);
    return ret;
}


int main(int argc, char**argv){
	std::thread temp_scanner(temp_scan,1);
	struct MHD_Daemon *daemon;
	daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, 8080, NULL, NULL, &answer, NULL, MHD_OPTION_END);
//	temp_scanner.join();
	while(true){
		std::cout<<"Floor 1 heating return temp " << measured_temperatures[0] << " °C     " << std::endl << "Floor 2 heating return temp " << measured_temperatures[1] << " °C     " <<std::endl;
		std::cout<<"Boiler water temp " << measured_temperatures[2] << " °C     " << std::endl << "Solar collector temp " << measured_temperatures[3] << " °C     " <<std::endl;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		std::cout<<"\033[4A";

		//Выключаем линии, если они более чем на 4 градуса горячее других
		set_switch(argv[0], 0, measured_temperatures[0] <= measured_temperatures[1] + 4);
		set_switch(argv[0], 1, measured_temperatures[1] <= measured_temperatures[0] + 4);

		//Включаем насос солнечного коллектора, если коллектор более чем на 4 градуса горячее бойлера
		if(switch_enabled[2])
			set_switch(argv[0], 2, measured_temperatures[3] > measured_temperatures[2] + 1);
		else
			set_switch(argv[0], 2, measured_temperatures[3] > measured_temperatures[2] + 4);
	}
//pump_test(argv[0],line_offset_1);
//pump_test(argv[0],line_offset_2);
//pump_test(argv[0],line_offset_3);
	return 0;
}
