#include <cstdint>
#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "dut.cpp"

#include <cxxrtl/cxxrtl_vcd.h>

const char *help_str =
"Usage: tb [--bin x.bin] [--port n] [--vcd x.vcd] [--dump start end] \\\n"
"          [--cycles n] [--cpuret] [--jtagdump x] [--jtagreplay x]\n"
"\n"
"    --vcd x.vcd      : Path to dump waveforms to\n"
"    --dump start end : Print out memory contents from start to end (exclusive)\n"
"                       after execution finishes. Can be passed multiple times.\n"
"    --cycles n       : Maximum number of cycles to run before exiting.\n"
"                       Default is 0 (no maximum).\n"
"    --port n         : Port number to listen for openocd remote bitbang. Sim\n"
"                       runs in lockstep with JTAG bitbang, not free-running.\n"
;

void exit_help(std::string errtext = "") {
	std::cerr << errtext << help_str;
	exit(-1);
}

int wait_for_connection(int server_fd, uint16_t port, struct sockaddr *sock_addr, socklen_t *sock_addr_len) {
	int sock_fd;
	printf("Waiting for connection on port %u\n", port);
	if (listen(server_fd, 3) < 0) {
		fprintf(stderr, "listen failed\n");
		exit(-1);
	}
	sock_fd = accept(server_fd, sock_addr, sock_addr_len);
	if (sock_fd < 0) {
		fprintf(stderr, "accept failed\n");
		exit(-1);
	}
	printf("Connected\n");
	return sock_fd;
}

static const int TCP_BUF_SIZE = 256;

int main(int argc, char **argv) {

	bool load_bin = false;
	std::string bin_path;
	bool dump_waves = false;
	std::string waves_path;
	std::vector<std::pair<uint32_t, uint32_t>> dump_ranges;
	int64_t max_cycles = 0;
	bool propagate_return_code = false;
	uint16_t port = 0;
	bool dump_jtag = false;
	std::string jtag_dump_path;
	bool replay_jtag = false;
	std::string jtag_replay_path;

    
	for (int i = 1; i < argc; ++i) {
		std::string s(argv[i]);
		if (s.rfind("--", 0) != 0) {
			std::cerr << "Unexpected positional argument " << s << "\n";
			exit_help("");
		}
		else if (s == "--vcd") {
			if (argc - i < 2)
				exit_help("Option --vcd requires an argument\n");
			dump_waves = true;
			waves_path = argv[i + 1];
			i += 1;
		}
		else if (s == "--jtagdump") {
			if (argc - i < 2)
				exit_help("Option --jtagdump requires an argument\n");
			dump_jtag = true;
			jtag_dump_path = argv[i + 1];
			i += 1;
		}
		else if (s == "--cycles") {
			if (argc - i < 2)
				exit_help("Option --cycles requires an argument\n");
			max_cycles = std::stol(argv[i + 1], 0, 0);
			i += 1;
		}
		else if (s == "--port") {
			if (argc - i < 2)
				exit_help("Option --port requires an argument\n");
			port = std::stol(argv[i + 1], 0, 0);
			i += 1;
		}
		else if (s == "--cpuret") {
			propagate_return_code = true;
		}
		else {
			std::cerr << "Unrecognised argument " << s << "\n";
			exit_help("");
		}
	}

	int server_fd, sock_fd;
	struct sockaddr_in sock_addr;
	int sock_opt = 1;
	socklen_t sock_addr_len = sizeof(sock_addr);
	char txbuf[TCP_BUF_SIZE], rxbuf[TCP_BUF_SIZE];
	int rx_ptr = 0, rx_remaining = 0, tx_ptr = 0;

	std::ofstream jtag_dump_fd;
	if (dump_jtag) {
		jtag_dump_fd.open(jtag_dump_path);
		if (!jtag_dump_fd.is_open()) {
			std::cerr << "Failed to open \"" << jtag_dump_path << "\"\n";
			return -1;
		}
	}

	if (port != 0) {
		server_fd = socket(AF_INET, SOCK_STREAM, 0);
		if (server_fd == 0) {
			fprintf(stderr, "socket creation failed\n");
			exit(-1);
		}

		int setsockopt_rc = setsockopt(
			server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
			&sock_opt, sizeof(sock_opt)
		);

		if (setsockopt_rc) {
			fprintf(stderr, "setsockopt failed\n");
			exit(-1);
		}

		sock_addr.sin_family = AF_INET;
		sock_addr.sin_addr.s_addr = INADDR_ANY;
		sock_addr.sin_port = htons(port);
		if (bind(server_fd, (struct sockaddr *)&sock_addr, sizeof(sock_addr)) < 0) {
			fprintf(stderr, "bind failed\n");
			exit(-1);
		}

		sock_fd = wait_for_connection(server_fd, port, (struct sockaddr *)&sock_addr, &sock_addr_len);
	}

	cxxrtl_design::p_tb top;

	std::ofstream waves_fd;
	cxxrtl::vcd_writer vcd;
	if (dump_waves) {
		waves_fd.open(waves_path);
		cxxrtl::debug_items all_debug_items;
		top.debug_info(&all_debug_items, /*scopes=*/nullptr, "");
		vcd.timescale(1, "us");
		vcd.add(all_debug_items);
	}

	top.step();
	top.p_tck.set<bool>(true);
	top.step();
	top.p_tck.set<bool>(false);
	top.p_trst__n.set<bool>(true);
	top.step();
	top.step(); // workaround for github.com/YosysHQ/yosys/issues/2780

	bool timed_out = false;
	for (int64_t cycle = 0; cycle < max_cycles || max_cycles == 0; ++cycle) {
		top.step();
		if (dump_waves)
			vcd.sample(cycle * 2);
		top.step();
		top.step(); // workaround for github.com/YosysHQ/yosys/issues/2780

		// If --port is specified, we run the simulator in lockstep with the
		// remote bitbang commands, to get more consistent simulation traces.
		// This slows down simulation quite a bit compared with normal
		// free-running.
		//
		// Most bitbang commands complete in one cycle (e.g. TCK/TMS/TDI
		// writes) but reads take 0 cycles, step=false.
		bool got_exit_cmd = false;
		bool step = false;
		if (port != 0 or replay_jtag) {
			while (!step) {
				if (rx_remaining > 0) {
					char c = rxbuf[rx_ptr++];
					--rx_remaining;

                    // printf("%c\n", c); 

					if (c == 'r' || c == 's') {
						top.p_trst__n.set<bool>(true);
						step = true;
					}
					else if (c == 't' || c == 'u') {
						top.p_trst__n.set<bool>(false);
					}
					else if (c >= '0' && c <= '7') {
						int mask = c - '0';
						top.p_tck.set<bool>(mask & 0x4);
						top.p_tms.set<bool>(mask & 0x2);
						top.p_tdi.set<bool>(mask & 0x1);
						step = true;
					}
					else if (c == 'R') {
						txbuf[tx_ptr++] = top.p_tdo.get<bool>() ? '1' : '0';
						if (tx_ptr >= TCP_BUF_SIZE || rx_remaining == 0) {
							send(sock_fd, txbuf, tx_ptr, 0);
							tx_ptr = 0;
						}
					}
					else if (c == 'Q') {
						printf("OpenOCD sent quit command\n");
						got_exit_cmd = true;
						step = true;
					}
				}
				else {
					// Potentially the last command was not a read command, but
					// OpenOCD is still waiting for a last response from its
					// last command packet before it sends us any more, so now is
					// the time to flush TX.
					if (tx_ptr > 0) {
						send(sock_fd, txbuf, tx_ptr, 0);
						tx_ptr = 0;
					}	
					rx_ptr = 0;
                    rx_remaining = read(sock_fd, &rxbuf, TCP_BUF_SIZE);
                    if (dump_jtag && rx_remaining > 0) {
                        jtag_dump_fd.write(rxbuf, rx_remaining);
                    }
					if (rx_remaining == 0) {
						if (port == 0) {
							// Presumably EOF, so quit.
							got_exit_cmd = true;
						}
						else {
							// The socket is closed. Wait for another connection.
							sock_fd = wait_for_connection(server_fd, port, (struct sockaddr *)&sock_addr, &sock_addr_len);
						}
					}
				}
			}
		}


		if (dump_waves) {
			// The extra step() is just here to get the bus responses to line up nicely
			// in the VCD (hopefully is a quick update)
			top.step();
			vcd.sample(cycle * 2 + 1);
			waves_fd << vcd.buffer;
			vcd.buffer.clear();
		}

		if (cycle + 1 == max_cycles) {
			printf("Max cycles reached\n");
			timed_out = true;
		}
		if (got_exit_cmd)
			break;
	}

	close(sock_fd);
	if (dump_jtag) {
		jtag_dump_fd.close();
	}
	// for (auto r : dump_ranges) {
	// 	printf("Dumping memory from %08x to %08x:\n", r.first, r.second);
	// 	for (int i = 0; i < r.second - r.first; ++i)
	// 		printf("%02x%c", memio.mem[r.first + i], i % 16 == 15 ? '\n' : ' ');
	// 	printf("\n");
	// }

	if (propagate_return_code && timed_out) {
		return -1;
	}
	else {
		return 0;
	}
}


