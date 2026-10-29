队伍：Apollo3Drelease
环境：Ubuntu 24.04 x86_64，CPU运行，无需联网。
服务器由组委会提供，本包仅启动己方队员。

普通比赛：
bash start.sh <服务器IP> [端口，默认60000]

点球罚球队员：
bash start_penalty_kicker.sh <服务器IP> [端口]

点球守门员：
bash start_penalty_goalie.sh <服务器IP> [端口]

停止己方队伍：
bash kill.sh

当前动作按并行服务器配置开发，不能直接视为适用于sequential模式。
单人空门射门已在本机验证进球并站稳。
普通比赛射门已编译，本机实测暂未进行。
完整对抗、正式点球规则和组委会环境尚未全面验收。
源码来源：https://github.com/XiangruiJiang/ApolloCodebase
