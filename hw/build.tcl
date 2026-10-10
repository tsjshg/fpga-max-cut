# =====================================================================
#  PYNQ-Z1 (XC7Z020) ひな形: DMA で DDR → 自作回路 → DDR
#
#    PS7 -M_AXI_GP0-> AXI Interconnect -> AXI DMA（制御レジスタ）
#    DDR -HP0-> SmartConnect -> DMA MM2S -> axis_cumsum -> DMA S2MM -> SmartConnect -HP0-> DDR
#
#  使い方（hw/ で）:
#    vivado.sh build build.tcl bd  [FCLK MHz]   ブロックデザインの検証だけ（1分弱）
#    vivado.sh build build.tcl all [FCLK MHz]   合成〜ビットストリーム（10分前後）
#  必ず bd を先に通す。いきなり all で落ちると数分無駄になる。
#  できたものは out/<tag>.bit と out/<tag>.hwh（PYNQ は同じ basename の2つを読む）。
#
#  自分の回路にするときに触るところ:
#    - rtl/ のファイル名とモジュール名（add_files と create_bd_cell）
#    - DMA の幅（入出力の幅が違うなら c_s_axis_s2mm_tdata_width など）
#    - 帯域が要るなら hp_ports を {0 2} にして DMA を2組（HP0/HP1 は PS 側で対なので避ける）
# =====================================================================
set stage [expr {$argc > 0 ? [lindex $argv 0] : "bd"}]
set fclk  [expr {$argc > 1 ? [lindex $argv 1] : 100}]
set hp_ports {0}

set part   xc7z020clg400-1
set tag    cumsum${fclk}
set proj   prj_$tag
set bd     design_1
set outdir [file normalize ./out]
file mkdir $outdir
puts "### stage = $stage / FCLK = $fclk MHz"

create_project $proj ./$proj -part $part -force
add_files -norecurse ./rtl/axis_cumsum.v
update_compile_order -fileset sources_1
create_bd_design $bd

# ---------- PS7 ----------
source ./ps7.tcl

# ---------- AXI DMA（単純転送・Scatter-Gather なし） ----------
# c_sg_length_width は1回の転送の最大バイト数（2^n - 1）。26 なら 64 MiB まで。
# 142.86 MHz 以上で落ちるときは、まずこの長さカウンタが最悪経路になっていないかを見る。
set dma [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma axi_dma_0]
set_property -dict [list \
  CONFIG.c_include_sg {0} \
  CONFIG.c_sg_include_stscntrl_strm {0} \
  CONFIG.c_include_mm2s {1} \
  CONFIG.c_include_s2mm {1} \
  CONFIG.c_include_mm2s_dre {0} \
  CONFIG.c_include_s2mm_dre {0} \
  CONFIG.c_m_axi_mm2s_data_width {64} \
  CONFIG.c_m_axis_mm2s_tdata_width {64} \
  CONFIG.c_m_axi_s2mm_data_width {64} \
  CONFIG.c_s_axis_s2mm_tdata_width {64} \
  CONFIG.c_mm2s_burst_size {256} \
  CONFIG.c_s2mm_burst_size {256} \
  CONFIG.c_sg_length_width {26} \
] $dma

# ---------- 自作回路 ----------
set core [create_bd_cell -type module -reference axis_cumsum core_0]
connect_bd_intf_net [get_bd_intf_pins axi_dma_0/M_AXIS_MM2S] [get_bd_intf_pins core_0/s_axis]
connect_bd_intf_net [get_bd_intf_pins core_0/m_axis]         [get_bd_intf_pins axi_dma_0/S_AXIS_S2MM]

# ---------- AXI の自動配線 ----------
# 制御: PS の GP0 → DMA のレジスタ
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
  -config {Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} \
           Master {/ps7_0/M_AXI_GP0} Slave {/axi_dma_0/S_AXI_LITE} \
           ddr_seg {Auto} intc_ip {New AXI Interconnect} master_apm {0}} \
  [get_bd_intf_pins axi_dma_0/S_AXI_LITE]

# データ: DMA の読み → HP0（SmartConnect を新しく立てる）
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
  -config {Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} \
           Master {/axi_dma_0/M_AXI_MM2S} Slave {/ps7_0/S_AXI_HP0} \
           ddr_seg {Auto} intc_ip {New AXI SmartConnect} master_apm {0}} \
  [get_bd_intf_pins ps7_0/S_AXI_HP0]

# データ: DMA の書き → 同じ SmartConnect に相乗り。
# ブレース {} の中では $smc が展開されないので、この行だけ二重引用符で組む。
set smc [lindex [get_bd_cells -quiet -filter {VLNV =~ "*:smartconnect:*"}] 0]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
  -config "Clk_master {Auto} Clk_slave {Auto} Clk_xbar {Auto} \
           Master {/axi_dma_0/M_AXI_S2MM} Slave {/ps7_0/S_AXI_HP0} \
           ddr_seg {Auto} intc_ip {$smc} master_apm {0}" \
  [get_bd_intf_pins axi_dma_0/M_AXI_S2MM]

# ---------- ストリームだけで繋いだセルのクロックとリセット ----------
# apply_bd_automation は AXI のインタフェースしか配線しない。
# proc_sys_reset のセル名は自動で付くので、名前でなく VLNV で探す。
set rst [lindex [get_bd_cells -quiet -filter {VLNV =~ "*:proc_sys_reset:*"}] 0]
if {$rst eq ""} { error "proc_sys_reset not found" }
connect_bd_net [get_bd_pins core_0/aclk]    [get_bd_pins ps7_0/FCLK_CLK0]
connect_bd_net [get_bd_pins core_0/aresetn] [get_bd_pins $rst/peripheral_aresetn]

assign_bd_address
regenerate_bd_layout
validate_bd_design
save_bd_design
puts "### block design OK"
foreach s [get_bd_addr_segs -quiet] { puts "###   $s" }
if {$stage ne "all"} { puts "### stage=bd: stopping here"; exit 0 }

# ---------- 合成〜ビットストリーム ----------
make_wrapper -files [get_files ./$proj/$proj.srcs/sources_1/bd/$bd/$bd.bd] -top
add_files -norecurse ./$proj/$proj.gen/sources_1/bd/$bd/hdl/${bd}_wrapper.v
set_property top ${bd}_wrapper [current_fileset]
update_compile_order -fileset sources_1

# -jobs は 2。Vivado は IP ごとに約 2.8 GB のプロセスを立てるので、8 だと OOM で殺される。
launch_runs impl_1 -to_step write_bitstream -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] != "100%"} { puts "### implementation failed"; exit 1 }

file copy -force ./$proj/$proj.runs/impl_1/${bd}_wrapper.bit $outdir/$tag.bit
set hwh [glob -nocomplain ./$proj/$proj.gen/sources_1/bd/$bd/hw_handoff/$bd.hwh]
file copy -force [lindex $hwh 0] $outdir/$tag.hwh

# open_run の後は STATS.* が取れなくなるので、WNS は先に読む。資源はレポートをファイルに出す。
set wns [get_property STATS.WNS [get_runs impl_1]]
open_run impl_1
report_utilization -file $outdir/${tag}_util.txt
report_timing_summary -file $outdir/${tag}_timing.txt
puts "### done: $outdir/$tag.bit and $tag.hwh"
puts "### FCLK $fclk_act MHz / WNS $wns ns"
if {$wns < 0} { puts "### WARNING: timing not met. Do not trust results at this clock" }
exit 0
