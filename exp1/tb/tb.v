module tb (
    input  wire       tck,
    input  wire       trst_n,
    input  wire       tms,
    input  wire       tdi,
    output wire       tdo,
    output wire [9:0] leds

);


    jtag_blinky #(
        .N_LEDS(10),
        .IDCODE(32'h1)
    ) xJTAG (
        .tck       (tck),
        .trst_n    (trst_n),
        .tms       (tms),
        .tdi       (tdi),
        .tdo       (tdo),
        .leds      (leds),
        .mirror_tck(),
        .mirror_tms(),
        .mirror_tdi(),
        .mirror_tdo()
    );



endmodule
