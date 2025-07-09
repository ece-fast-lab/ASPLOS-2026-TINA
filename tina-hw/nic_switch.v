
// Author: Jinghan Huang, University of Illinois Urbana-Champaign

`resetall
`timescale 1ns / 1ps
`default_nettype none

module nic_switch #
(
    parameter AXIS_DATA_WIDTH = 512,
    parameter AXIS_KEEP_WIDTH = AXIS_DATA_WIDTH/8,
    parameter ILA_ENABLE = 0
) 
(
    //******* TX 0 *******
    input wire                          tx0_clk,
    input wire                          tx0_rst,

    output  wire [AXIS_DATA_WIDTH-1:0]  tx0_axis_tdata,
    output  wire [AXIS_KEEP_WIDTH-1:0]  tx0_axis_tkeep,
    output  wire                        tx0_axis_tvalid,
    input wire                          tx0_axis_tready,
    output  wire                        tx0_axis_tlast,
    output  wire [16+1-1:0]             tx0_axis_tuser,

    //******* TX 1 *******
    input wire                          tx1_clk,
    input wire                          tx1_rst,

    output  wire [AXIS_DATA_WIDTH-1:0]  tx1_axis_tdata,
    output  wire [AXIS_KEEP_WIDTH-1:0]  tx1_axis_tkeep,
    output  wire                        tx1_axis_tvalid,
    input wire                          tx1_axis_tready,
    output  wire                        tx1_axis_tlast,
    output  wire [16+1-1:0]             tx1_axis_tuser,

    //******* RX 0 *******
    input wire                          rx0_clk,
    input wire                          rx0_rst,

    input wire [AXIS_DATA_WIDTH-1:0]    rx0_axis_tdata,
    input wire [AXIS_KEEP_WIDTH-1:0]    rx0_axis_tkeep,
    input wire                          rx0_axis_tvalid,
    input wire                          rx0_axis_tlast,
    input wire [80+1-1:0]               rx0_axis_tuser,

    //******* RX 01 *******
    input wire                          rx1_clk,
    input wire                          rx1_rst,

    input wire [AXIS_DATA_WIDTH-1:0]    rx1_axis_tdata,
    input wire [AXIS_KEEP_WIDTH-1:0]    rx1_axis_tkeep,
    input wire                          rx1_axis_tvalid,
    input wire                          rx1_axis_tlast,
    input wire [80+1-1:0]               rx1_axis_tuser,

    //****** General ******
    input wire                          clk_250mhz,
    input wire                          rst_250mhz,

    input wire [31:0]                   consumption_rate,
    input wire [31:0]                   forward_threshold_3,
    input wire [31:0]                   forward_threshold_1,
    input wire [31:0]                   forward_threshold_2,
    input wire [31:0]                   forward_port_1,
    input wire [31:0]                   unused2
);

/*******************************************************************************/
/*************************** Active Buffer Estimator ***************************/
/*******************************************************************************/

// counter for RX0 clock cycles to keep track of 1 us. (Consumpution Rate is fed in bytes per us)
reg [31:0] rx0_clk_counter;
wire should_decrement;
always @(posedge rx0_clk) begin
    if (rx0_rst) begin
        rx0_clk_counter <= 32'b0;
    end
    else begin
        if (rx0_clk_counter == 32'd322) begin
            rx0_clk_counter <= 32'b0;
        end
        else begin
            rx0_clk_counter <= rx0_clk_counter + 1'b1;
        end
    end
end
assign should_decrement = (rx0_clk_counter == 32'd322);

reg [31:0] active_buffer_size_estimator_t0;
reg [31:0] active_buffer_size_estimator_t1;
reg [31:0] active_buffer_size_estimator_t0_next;
reg [31:0] active_buffer_size_estimator_t1_next;
reg [31:0] t0_next, t1_next;

reg ingress_tier;
reg ingress_tier_next;
reg processing_tier;
reg processing_tier_next;

always @(posedge rx0_clk) begin
    if (rx0_rst) begin
        active_buffer_size_estimator_t0 <= 32'b0;
        active_buffer_size_estimator_t1 <= 32'b0;
        ingress_tier <= 1'b0;
        processing_tier <= 1'b0;
    end
    else begin
        active_buffer_size_estimator_t0 <= $signed(active_buffer_size_estimator_t0_next) < 0 ? 32'b0 : active_buffer_size_estimator_t0_next;
        active_buffer_size_estimator_t1 <= $signed(active_buffer_size_estimator_t1_next) < 0 ? 32'b0 : active_buffer_size_estimator_t1_next;
        ingress_tier <= ingress_tier_next;
        processing_tier <= processing_tier_next;
    end
end

always @(*) begin
  case (ingress_tier)
    1'b0: 
      ingress_tier_next = (active_buffer_size_estimator_t0 >= forward_threshold_1);
    1'b1:
      ingress_tier_next = ~(active_buffer_size_estimator_t1 >= forward_threshold_2 ||
                             active_buffer_size_estimator_t0 <  forward_threshold_3);
    default:
      ingress_tier_next = 1'b0;
  endcase

  case (processing_tier)
    1'b0:
      processing_tier_next = (active_buffer_size_estimator_t0 == 0 &&
                              active_buffer_size_estimator_t1 >  0);
    1'b1:
      processing_tier_next = (active_buffer_size_estimator_t1 != 0);
    default:
      processing_tier_next = 1'b0; // safe default assignment
  endcase
end


always @(*) begin
    t0_next = active_buffer_size_estimator_t0;
    t1_next = active_buffer_size_estimator_t1;
    
    // Add 64 to the appropriate estimator when valid.
    if (rx0_axis_tvalid) begin
        if (ingress_tier)
            t1_next = t1_next + 32'd64;
        else
            t0_next = t0_next + 32'd64;
    end

    if (should_decrement) begin
        if (processing_tier)
            t1_next = t1_next - consumption_rate;
        else
            t0_next = t0_next - consumption_rate;
    end

    active_buffer_size_estimator_t0_next = t0_next;
    active_buffer_size_estimator_t1_next = t1_next;
end

/*******************************************************************************/
/********************************** Timestamp **********************************/
/*******************************************************************************/

reg [31:0] timestamp_counter;
wire [31:0] timestamp_counter_rx1_clk_next;
wire [31:0] timestamp_counter_rx0_clk_next;

always @(posedge clk_250mhz) begin
    if (rst_250mhz) begin
        timestamp_counter <= 64'b0;
    end
    else begin
        timestamp_counter <= timestamp_counter + 1'b1;
    end
end

xpm_cdc_gray #(
   .DEST_SYNC_FF(4),          // DECIMAL; range: 2-10
   .INIT_SYNC_FF(0),          // DECIMAL; 0=disable simulation init values, 1=enable simulation init values
   .REG_OUTPUT(1),            // DECIMAL; 0=disable registered output, 1=enable registered output
   .SIM_ASSERT_CHK(0),        // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
   .SIM_LOSSLESS_GRAY_CHK(0), // DECIMAL; 0=disable lossless check, 1=enable lossless check
   .WIDTH(32)                  // DECIMAL; range: 2-32
)
xpm_cdc_gray_inst_rx0 (
   .dest_out_bin(timestamp_counter_rx0_clk_next),   // WIDTH-bit output: Binary input bus (src_in_bin) synchronized to
                                                    // destination clock domain. This output is combinatorial unless REG_OUTPUT
                                                    // is set to 1.

   .dest_clk(rx0_clk),                  // 1-bit input: Destination clock.
   .src_clk(clk_250mhz),                // 1-bit input: Source clock.
   .src_in_bin(timestamp_counter)       // WIDTH-bit input: Binary input bus that will be synchronized to the
                                        // destination clock domain.

);


xpm_cdc_gray #(
   .DEST_SYNC_FF(4),          // DECIMAL; range: 2-10
   .INIT_SYNC_FF(0),          // DECIMAL; 0=disable simulation init values, 1=enable simulation init values
   .REG_OUTPUT(1),            // DECIMAL; 0=disable registered output, 1=enable registered output
   .SIM_ASSERT_CHK(0),        // DECIMAL; 0=disable simulation messages, 1=enable simulation messages
   .SIM_LOSSLESS_GRAY_CHK(0), // DECIMAL; 0=disable lossless check, 1=enable lossless check
   .WIDTH(32)                  // DECIMAL; range: 2-32
)
xpm_cdc_gray_inst_rx1 (
   .dest_out_bin(timestamp_counter_rx1_clk_next),   // WIDTH-bit output: Binary input bus (src_in_bin) synchronized to
                                                    // destination clock domain. This output is combinatorial unless REG_OUTPUT
                                                    // is set to 1.

   .dest_clk(rx1_clk),                  // 1-bit input: Destination clock.
   .src_clk(clk_250mhz),                // 1-bit input: Source clock.
   .src_in_bin(timestamp_counter)       // WIDTH-bit input: Binary input bus that will be synchronized to the
                                        // destination clock domain.
);

// assign timestamp_counter_rx0_clk_next = timestamp_counter;
// assign timestamp_counter_rx1_clk_next = timestamp_counter;


/*******************************************************************************/
/********************** RX0 - find 1st and 2nd 64B of pkt **********************/
/*******************************************************************************/
// rx0 tfirst
reg rx0_axis_tfirst_reg;
reg rx0_axis_tfirst_next;

//rx0 tsecond
reg rx0_axis_seen_first;

always @(posedge rx0_clk) begin
    if (rx0_rst) begin
        rx0_axis_tfirst_reg <= 1'b1;
        rx0_axis_seen_first <= 1'b0;
    end else begin
        rx0_axis_tfirst_reg <= rx0_axis_tfirst_next;
        if (rx0_axis_tfirst_reg && ~rx0_axis_tfirst_next)
            rx0_axis_seen_first <= 1'b1;
        if (rx0_axis_tvalid && rx0_axis_seen_first) begin
            rx0_axis_seen_first <= 1'b0;
        end
    end
end


always @* begin
    if (rx0_axis_tvalid && !rx0_axis_tlast) begin
        rx0_axis_tfirst_next = 1'b0;
    end else if (rx0_axis_tvalid && rx0_axis_tlast) begin
        rx0_axis_tfirst_next = 1'b1;
    end else begin
        rx0_axis_tfirst_next = rx0_axis_tfirst_reg;
    end
end

/*******************************************************************************/
/********************** RX1 - find 1st and 2nd 64B of pkt **********************/
/*******************************************************************************/
// rx1 tfirst
reg rx1_axis_tfirst_reg;
reg rx1_axis_tfirst_next;

//rx1 tsecond
reg rx1_axis_seen_first;

always @(posedge rx1_clk) begin
    if (rx1_rst) begin
        rx1_axis_tfirst_reg <= 1'b1;
        rx1_axis_seen_first <= 1'b0;
    end else begin
        rx1_axis_tfirst_reg <= rx1_axis_tfirst_next;
        if (rx1_axis_tfirst_reg && ~rx1_axis_tfirst_next)
            rx1_axis_seen_first <= 1'b1;
        if (rx1_axis_tvalid && rx1_axis_seen_first) begin
            rx1_axis_seen_first <= 1'b0;
        end
    end
end


always @* begin
    if (rx1_axis_tvalid && !rx1_axis_tlast) begin
        rx1_axis_tfirst_next = 1'b0;
    end else if (rx1_axis_tvalid && rx1_axis_tlast) begin
        rx1_axis_tfirst_next = 1'b1;
    end else begin
        rx1_axis_tfirst_next = rx1_axis_tfirst_reg;
    end
end

/*******************************************************************************/
/*********************** Change ports and add timestamp ************************/
/*******************************************************************************/
// packet dst port change
reg [AXIS_DATA_WIDTH-1:0]    rx0_change_axis_tdata_reg;
reg [AXIS_KEEP_WIDTH-1:0]    rx0_change_axis_tkeep_reg;
reg                          rx0_change_axis_tvalid_reg;
reg                          rx0_change_axis_tlast_reg;
reg [80+1-1:0]               rx0_change_axis_tuser_reg;

wire [AXIS_DATA_WIDTH-1:0]    rx0_change_axis_tdata;
wire [AXIS_KEEP_WIDTH-1:0]    rx0_change_axis_tkeep;
wire                          rx0_change_axis_tvalid;
wire                          rx0_change_axis_tlast;
wire [80+1-1:0]               rx0_change_axis_tuser;

assign rx0_change_axis_tdata = rx0_change_axis_tdata_reg;
assign rx0_change_axis_tkeep = rx0_change_axis_tkeep_reg;
assign rx0_change_axis_tvalid = rx0_change_axis_tvalid_reg;
assign rx0_change_axis_tlast = rx0_change_axis_tlast_reg;
assign rx0_change_axis_tuser = rx0_change_axis_tuser_reg;


// packet dst port change
reg [AXIS_DATA_WIDTH-1:0]    rx1_change_axis_tdata_reg;
reg [AXIS_KEEP_WIDTH-1:0]    rx1_change_axis_tkeep_reg;
reg                          rx1_change_axis_tvalid_reg;
reg                          rx1_change_axis_tlast_reg;
reg [80+1-1:0]               rx1_change_axis_tuser_reg;

wire [AXIS_DATA_WIDTH-1:0]    rx1_change_axis_tdata;
wire [AXIS_KEEP_WIDTH-1:0]    rx1_change_axis_tkeep;
wire                          rx1_change_axis_tvalid;
wire                          rx1_change_axis_tlast;
wire [80+1-1:0]               rx1_change_axis_tuser;

assign rx1_change_axis_tdata = rx1_change_axis_tdata_reg;
assign rx1_change_axis_tkeep = rx1_change_axis_tkeep_reg;
assign rx1_change_axis_tvalid = rx1_change_axis_tvalid_reg;
assign rx1_change_axis_tlast = rx1_change_axis_tlast_reg;
assign rx1_change_axis_tuser = rx1_change_axis_tuser_reg;

always @(posedge rx0_clk) begin
    //Change Port on first 64B RX
    if ( ingress_tier == 1'b1 && (rx0_axis_tfirst_reg == 1'b1 && rx0_axis_tdata[303:296] == forward_port_1[7:0] && rx0_axis_tdata[295:288] == forward_port_1[15:8])) begin
        if (rx0_axis_tdata[335:328] >= 8'h2) begin
            rx0_change_axis_tdata_reg <= {rx0_axis_tdata[511:336], rx0_axis_tdata[335:328] - 8'h2 /*UDP CheckSum*/, rx0_axis_tdata[327:304], rx0_axis_tdata[303:296] + 8'h2 /*dst_port*/, rx0_axis_tdata[295:208], rx0_axis_tdata[207:200] - 8'h2 /*IP CheckSum*/ , rx0_axis_tdata[199:0]};
        end else begin
            rx0_change_axis_tdata_reg <= {rx0_axis_tdata[511:336], rx0_axis_tdata[335:320] - 16'h0201 /*UDP CheckSum*/, rx0_axis_tdata[319:304], rx0_axis_tdata[303:296] + 8'h2 /*dst_port*/, rx0_axis_tdata[295:208], rx0_axis_tdata[207:192] - 16'h0201 /*IP CheckSum*/, rx0_axis_tdata[191:0]};
        end
    end 
    //Add timestamp on second 64B RX
    else if (rx0_axis_seen_first && rx0_axis_tvalid == 1'b1) begin
        rx0_change_axis_tdata_reg <= {rx0_axis_tdata[511:32], timestamp_counter_rx0_clk_next};
    end 
    else 
    begin
        rx0_change_axis_tdata_reg <= rx0_axis_tdata;
    end

    rx0_change_axis_tkeep_reg <= rx0_axis_tkeep;
    rx0_change_axis_tvalid_reg <= rx0_axis_tvalid;
    rx0_change_axis_tlast_reg <= rx0_axis_tlast;
    rx0_change_axis_tuser_reg <= rx0_axis_tuser;
end

always @(posedge rx1_clk) begin
    //Add timestamp on second (64+8)B RX
    if (rx1_axis_seen_first && rx1_axis_tvalid == 1'b1) begin
        rx1_change_axis_tdata_reg <= {rx1_axis_tdata[511:96], timestamp_counter_rx1_clk_next, rx1_axis_tdata[63:0]};
    end else begin
        rx1_change_axis_tdata_reg <= rx1_axis_tdata;
    end

    rx1_change_axis_tkeep_reg <= rx1_axis_tkeep;
    rx1_change_axis_tvalid_reg <= rx1_axis_tvalid;
    rx1_change_axis_tlast_reg <= rx1_axis_tlast;
    rx1_change_axis_tuser_reg <= rx1_axis_tuser;
end

/*******************************************************************************/
/**************************** RX --> TX Cross Domain ***************************/
/*******************************************************************************/
// FIFO rx0 -> tx1
axis_data_fifo_async rx0_tx1_fifo_inst (
    .s_axis_aclk(rx0_clk),
    .s_axis_aresetn(~rx0_rst),
    .m_axis_aclk(tx1_clk),
    // AXI input
    .s_axis_tdata(rx0_change_axis_tdata),
    .s_axis_tkeep(rx0_change_axis_tkeep),
    .s_axis_tvalid(rx0_change_axis_tvalid),
    .s_axis_tready(),
    .s_axis_tlast(rx0_change_axis_tlast),
    .s_axis_tuser(17'b0),
    // AXI output
    .m_axis_tdata(tx1_axis_tdata),
    .m_axis_tkeep(tx1_axis_tkeep),
    .m_axis_tvalid(tx1_axis_tvalid),
    .m_axis_tready(tx1_axis_tready),
    .m_axis_tlast(tx1_axis_tlast),
    .m_axis_tuser(tx1_axis_tuser)
);

// FIFO rx1 -> tx0
axis_data_fifo_async rx1_tx0_fifo_inst (
    .s_axis_aclk(rx1_clk),
    .s_axis_aresetn(~rx1_rst),
    .m_axis_aclk(tx0_clk),
    // AXI input
    .s_axis_tdata(rx1_change_axis_tdata),
    .s_axis_tkeep(rx1_change_axis_tkeep),
    .s_axis_tvalid(rx1_change_axis_tvalid),
    .s_axis_tready(),
    .s_axis_tlast(rx1_change_axis_tlast),
    .s_axis_tuser(17'b0),
    // AXI output
    .m_axis_tdata(tx0_axis_tdata),
    .m_axis_tkeep(tx0_axis_tkeep),
    .m_axis_tvalid(tx0_axis_tvalid),
    .m_axis_tready(tx0_axis_tready),
    .m_axis_tlast(tx0_axis_tlast),
    .m_axis_tuser(tx0_axis_tuser)
);

// // FIFO rx1 -> tx0
// axis_async_fifo #(.DATA_WIDTH(512)) rx1_tx0_fifo_inst (
//     .s_clk(rx1_clk),
//     .s_rst(rx1_rst),
//     .m_clk(tx0_clk),
//     // AXI input
//     .s_axis_tdata(rx1_change_axis_tdata),
//     .s_axis_tkeep(rx1_change_axis_tkeep),
//     .s_axis_tvalid(rx1_change_axis_tvalid),
//     .s_axis_tready(),
//     .s_axis_tlast(rx1_change_axis_tlast),
//     .s_axis_tuser(17'b0),
//     // AXI output
//     .m_axis_tdata(tx0_axis_tdata),
//     .m_axis_tkeep(tx0_axis_tkeep),
//     .m_axis_tvalid(tx0_axis_tvalid),
//     .m_axis_tready(tx0_axis_tready),
//     .m_axis_tlast(tx0_axis_tlast),
//     .m_axis_tuser(tx0_axis_tuser)
// );


// // FIFO rx0 -> tx1
// axis_async_fifo  #(.DATA_WIDTH(512)) rx0_tx1_fifo_inst (
//     .s_clk(rx0_clk),
//     .s_rst(rx0_rst),
//     .m_clk(tx1_clk),
//     // AXI input
//     .s_axis_tdata(rx0_change_axis_tdata),
//     .s_axis_tkeep(rx0_change_axis_tkeep),
//     .s_axis_tvalid(rx0_change_axis_tvalid),
//     .s_axis_tready(),
//     .s_axis_tlast(rx0_change_axis_tlast),
//     .s_axis_tuser(17'b0),
//     // AXI output
//     .m_axis_tdata(tx1_axis_tdata),
//     .m_axis_tkeep(tx1_axis_tkeep),
//     .m_axis_tvalid(tx1_axis_tvalid),
//     .m_axis_tready(tx1_axis_tready),
//     .m_axis_tlast(tx1_axis_tlast),
//     .m_axis_tuser(tx1_axis_tuser)
// );



endmodule

`resetall