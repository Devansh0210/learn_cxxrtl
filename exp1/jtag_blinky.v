module jtag_blinky #(
    parameter N_LEDS = 10,
    parameter IDCODE = 32'hdeadf001
) (
    input  wire tck,
    input  wire trst_n,
    input  wire tms,
    input  wire tdi,
    output reg  tdo,

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

    reg [3:0] cur_tap_state, next_tap_state;

    localparam S_RESET = 4'd0;
    localparam S_IDLE = 4'd1;
    localparam S_SELECT_DR = 4'd2;
    localparam S_CAPTURE_DR = 4'd3;
    localparam S_SHIFT_DR = 4'd4;
    localparam S_EXIT1_DR = 4'd5;
    localparam S_PAUSE_DR = 4'd6;
    localparam S_EXIT2_DR = 4'd7;
    localparam S_UPDATE_DR = 4'd8;
    localparam S_SELECT_IR = 4'd9;
    localparam S_CAPTURE_IR = 4'd10;
    localparam S_SHIFT_IR = 4'd11;
    localparam S_EXIT1_IR = 4'd12;
    localparam S_PAUSE_IR = 4'd13;
    localparam S_EXIT2_IR = 4'd14;
    localparam S_UPDATE_IR = 4'd15;


    localparam IRLEN = 4;

    localparam IR_IDCODE = 4'b0001;
    localparam IR_BLINK = 4'b1110;

    localparam DRLEN = 32;


    reg [IRLEN-1:0] ir_shift;
    reg [IRLEN-1:0] ir;


    always @(posedge tck or negedge trst_n) begin
        if (!trst_n) begin
            cur_tap_state <= S_RESET;
        end else begin
            cur_tap_state <= next_tap_state;
        end
    end

    always @(*) begin
        next_tap_state = cur_tap_state;
        case (cur_tap_state)
            S_RESET:      next_tap_state = tms ? cur_tap_state : S_IDLE;
            S_IDLE:       next_tap_state = tms ? S_SELECT_DR : cur_tap_state;
            S_SELECT_DR:  next_tap_state = tms ? S_SELECT_IR : S_CAPTURE_DR;
            S_CAPTURE_DR: next_tap_state = tms ? S_EXIT1_DR : S_SHIFT_DR;
            S_SHIFT_DR:   next_tap_state = tms ? S_EXIT1_DR : cur_tap_state;
            S_EXIT1_DR:   next_tap_state = tms ? S_UPDATE_DR : S_PAUSE_DR;
            S_PAUSE_DR:   next_tap_state = tms ? S_EXIT2_DR : cur_tap_state;
            S_EXIT2_DR:   next_tap_state = tms ? S_UPDATE_DR : cur_tap_state;
            S_UPDATE_DR:  next_tap_state = tms ? S_SELECT_DR : S_IDLE;
            S_SELECT_IR:  next_tap_state = tms ? S_RESET : S_CAPTURE_IR;
            S_CAPTURE_IR: next_tap_state = tms ? S_EXIT1_IR : S_SHIFT_IR;
            S_SHIFT_IR:   next_tap_state = tms ? S_EXIT1_IR : cur_tap_state;
            S_EXIT1_IR:   next_tap_state = tms ? S_UPDATE_IR : S_PAUSE_IR;
            S_PAUSE_IR:   next_tap_state = tms ? S_EXIT2_IR : cur_tap_state;
            S_EXIT2_IR:   next_tap_state = tms ? S_UPDATE_IR : S_SHIFT_IR;
            S_UPDATE_IR:  next_tap_state = tms ? S_SELECT_DR : S_IDLE;
            default:      next_tap_state = cur_tap_state;
        endcase
    end

    always @(posedge tck or negedge trst_n) begin
        if (!trst_n) begin
            ir_shift <= {IRLEN{1'b0}};
            ir       <= IR_IDCODE;
        end else begin
            if (cur_tap_state == S_RESET) begin
                ir_shift <= {IRLEN{1'b0}};
                ir       <= IR_IDCODE;
            end else if (cur_tap_state == S_CAPTURE_IR) begin
                ir_shift <= ir;
            end else if (cur_tap_state == S_SHIFT_IR) begin
                ir_shift <= {tdi, ir_shift[IRLEN-1:1]};
            end else if (cur_tap_state == S_UPDATE_IR) begin
                ir <= ir_shift;
            end
        end
    end


    reg [ DRLEN-1:0] dr_shift;
    reg [N_LEDS-1:0] led_reg;

    always @(posedge tck or negedge trst_n) begin
        if (!trst_n) begin
            dr_shift <= {DRLEN{1'b0}};
        end else begin
            if (cur_tap_state == S_RESET) begin
                dr_shift <= {DRLEN{1'b0}};
            end else if (cur_tap_state == S_CAPTURE_DR) begin
                if (ir == IR_BLINK) begin
                    dr_shift <= {DRLEN - N_LEDS{1'b0}};
                end else if (ir == IDCODE) begin
                    dr_shift <= IDCODE;
                end else begin
                    dr_shift <= {DRLEN{1'b0}};
                end
            end else if (cur_tap_state == S_SHIFT_DR) begin
                dr_shift <= {tdi, dr_shift[DRLEN-1:1]};
                if (ir == IR_BLINK) begin
                    dr_shift[N_LEDS-1:0] <= tdi;
                end else if (ir == IR_IDCODE) begin
                    dr_shift[31] <= tdi;
                end else begin
                    dr_shift[0] <= tdi;
                end
            end else if (cur_tap_state == S_UPDATE_DR) begin
                led_reg <= dr_shift[DRLEN-1:DRLEN-N_LEDS];
            end
        end
    end


    always @(negedge tck or negedge trst_n) begin
        if (!trst_n) begin
            tdo <= 1'b0;
        end else begin
            tdo <= (cur_tap_state == S_SHIFT_IR) ? ir_shift[0] : (cur_tap_state == S_SHIFT_DR) ? dr_shift[0] : 1'b0;
        end
    end


endmodule
