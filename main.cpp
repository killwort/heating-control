#include <cstdlib>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <gpiod.hpp>
#include <iostream>
#include <fstream>
#include <thread>
#include <microhttpd.h>
#include <string>

extern double measured_temperatures[4];
extern int measured_adc[4];

void temp_scan(int id);

const ::std::filesystem::path chip_path("/dev/gpiochip0");
int switch_address[3]={199,201,198};//198,201};
bool switch_enabled[3]={false};
std::time_t switch_enabled_time[3]={0};
double line_delta = 4.0;
double line_balance = 0.0;
double solar_on = 4.0;
double solar_off = 1.0;

void set_switch(char* arg, int channel, bool value){
	if(!value&&std::time_t(nullptr)-switch_enabled_time[channel]<30)return;
	auto chip=::gpiod::chip(chip_path);
	auto line=chip.get_line(switch_address[channel]);
	auto req=::gpiod::line_request{arg,::gpiod::line_request::DIRECTION_OUTPUT,0};
	line.request(req);
	line.set_direction_output();
	line.set_value(value?1:0);
	if(!switch_enabled[channel]&&value)switch_enabled_time[channel]=std::time_t(nullptr);
	switch_enabled[channel]=value;
}

MHD_Result handle_arg (void *cls, enum MHD_ValueKind kind, const char *key, const char *value){
	std::cout<<"ARG "<<key<<" = "<<value<<std::endl;
	if(strncmp(key,"lineDelta",9)==0 && value){
		line_delta = atof(value);
		if(line_delta<=1.0)line_delta=1.0;
	}else if(strncmp(key,"lineBalance",11)==0 && value)
		line_balance = atof(value);
	else if(strncmp(key,"solarOn",7)==0 && value)
		solar_on = atof(value);
	else if(strncmp(key,"solarOff",8)==0 && value)
		solar_off = atof(value);
	if(solar_off>solar_on-1.0)solar_off=solar_on-1.0;
	return MHD_YES;
}

MHD_Result answer(void *cls, struct MHD_Connection *connection,
           const char *url, const char *method,
           const char *ver, const char *upload_data,
           size_t *upload_data_size, void **con_cls)
{
	std::stringstream response;
	if(strncmp(url, "/configure",10)==0){
		MHD_get_connection_values(connection,MHD_GET_ARGUMENT_KIND,&handle_arg,NULL);
		std::filesystem::create_directories("/var/lib/heating-control");
		std::ofstream config("/var/lib/heating-control/config",std::ios::binary);
		config.write(reinterpret_cast<char*>(&line_delta), sizeof line_delta);
		config.write(reinterpret_cast<char*>(&line_balance), sizeof line_balance);
		config.write(reinterpret_cast<char*>(&solar_on), sizeof solar_on);
		config.write(reinterpret_cast<char*>(&solar_off), sizeof solar_off);
	}
	else if(strncmp(url, "/metrics", 8)==0){
		response<<"measured_temp{place=\"floor1Return\"} "<<measured_temperatures[1]<<std::endl;
		response<<"measured_temp{place=\"floor2Return\"} "<<measured_temperatures[0]<<std::endl;
		response<<"measured_temp{place=\"boiler\"} "<<measured_temperatures[2]<<std::endl;
		response<<"measured_temp{place=\"solarCollector\"} "<<measured_temperatures[3]<<std::endl;
		response<<"measured_adc{place=\"floor1Return\"} "<<measured_adc[1]<<std::endl;
		response<<"measured_adc{place=\"floor2Return\"} "<<measured_adc[0]<<std::endl;
		response<<"measured_adc{place=\"boiler\"} "<<measured_adc[2]<<std::endl;
		response<<"measured_adc{place=\"solarCollector\"} "<<measured_adc[3]<<std::endl;
		response<<"pump_state{place=\"floor1\"} "<<(switch_enabled[0]?1:0)<<std::endl;
		response<<"pump_state{place=\"floor2\"} "<<(switch_enabled[1]?1:0)<<std::endl;
		response<<"pump_state{place=\"solarCollector\"} "<<(switch_enabled[2]?1:0)<<std::endl;
		response<<"configuration{parameter=\"lineDelta\"} "<<line_delta<<std::endl;
		response<<"configuration{parameter=\"lineBalance\"} "<<line_balance<<std::endl;
		response<<"configuration{parameter=\"solarOn\"} "<<solar_on<<std::endl;
		response<<"configuration{parameter=\"solarOff\"} "<<solar_off<<std::endl;
	}else{
		response<<"{\"measured\":{\"floor1ReturnTemp\":";
		response<<measured_temperatures[1]<<",\"floor2ReturnTemp\":"<<measured_temperatures[0];
		response<<",\"boilerTemp\":"<<measured_temperatures[2]<<",\"solarCollectorTemp\":"<<measured_temperatures[3];
		response<<"},\"control\":{\"solarPumpEnabled\":"<<(switch_enabled[2]?"true":"false");
		response<<",\"floor1PumpEnabled\":"<<(switch_enabled[0]?"true":"false");
		response<<",\"floor2PumpEnabled\":"<<(switch_enabled[1]?"true":"false");
		response<<"},\"configuration\":{\"lineDelta\":"<<(float)line_delta<<",\"lineBalance\":"<<(float)line_balance;
		response<<",\"solarOn\""<<(float)solar_on<<",\"solarOff\":"<<(float)solar_off<<"}}";
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
	std::ifstream config("/var/lib/heating-control/config",std::ios::binary);
	if(config.good()){
		config.read(reinterpret_cast<char*>(&line_delta), sizeof line_delta);
		config.read(reinterpret_cast<char*>(&line_balance), sizeof line_balance);
		config.read(reinterpret_cast<char*>(&solar_on), sizeof solar_on);
		config.read(reinterpret_cast<char*>(&solar_off), sizeof solar_off);
	}
	std::thread temp_scanner(temp_scan,1);
	struct MHD_Daemon *daemon;
	daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, 8080, NULL, NULL, &answer, NULL, MHD_OPTION_END);
	double f1, f2;
	while(true){
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		//Выключаем линии, если они более чем на line_delta градуса горячее других
		f1 = measured_temperatures[1];
		f2 = measured_temperatures[0] - line_balance;
		set_switch(argv[0], 0, f1 - f2 <= line_delta);
		set_switch(argv[0], 1, f2 - f1 <= line_delta);

		//Включаем насос солнечного коллектора, если коллектор более чем на 4 градуса горячее бойлера
		if(switch_enabled[2])
			set_switch(argv[0], 2, measured_temperatures[3] > measured_temperatures[2] + solar_off);
		else
			set_switch(argv[0], 2, measured_temperatures[3] > measured_temperatures[2] + solar_on);
	}
	return 0;
}
