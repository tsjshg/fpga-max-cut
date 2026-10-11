# 配置配線済みの checkpoint を開いて、階層ごとの資源をファイルに出す（hw/ で叩く）
#   vivado.sh build util_hier.tcl mc125_k48 [impl|synth|core]   （core: 配置に失敗したときに自作回路の合成の結果を見る）
set tag [lindex $argv 0]
set which [expr {$argc > 1 ? [lindex $argv 1] : "impl"}]
if {$which eq "synth"} {
  open_checkpoint ./prj_$tag/prj_$tag.runs/synth_1/design_1_wrapper.dcp
} elseif {$which eq "core"} {
  # 自作回路だけの合成結果（OOC）。全体の合成では黒箱になっている
  open_checkpoint ./prj_$tag/prj_$tag.runs/design_1_core_0_0_synth_1/design_1_core_0_0.dcp
} else {
  open_checkpoint ./prj_$tag/prj_$tag.runs/impl_1/design_1_wrapper_routed.dcp
}
report_utilization -hierarchical -hierarchical_depth 6 -file ./out/${tag}_util_hier_${which}.txt
exit 0
