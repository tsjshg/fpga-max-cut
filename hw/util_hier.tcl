# 配置配線済みの checkpoint を開いて、階層ごとの資源をファイルに出す（hw/ で叩く）
#   vivado.sh build util_hier.tcl mc125_k48
set tag [lindex $argv 0]
open_checkpoint ./prj_$tag/prj_$tag.runs/impl_1/design_1_wrapper_routed.dcp
report_utilization -hierarchical -hierarchical_depth 6 -file ./out/${tag}_util_hier.txt
exit 0
