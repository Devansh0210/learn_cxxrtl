module jtag_blinky #(
    parameter int N_LEDS = 10,
    parameter int IDCODE = 12
) (
    input  wire tck,
    input  wire tms,
    input  wire tdi,
    output wire tdo,

    output wire [N_LEDS-1:0] leds,
    output wire              mirror_tck,
    output wire              mirror_tms,
    output wire              mirror_tdi,
    output wire              mirror_tdo
);

    assign mirror_tck = tck;
    assign mirror_tdi = tdi;
    assign mirror_tms = tms;
    assign mirror_tdo = tdo;

    
endmodule
