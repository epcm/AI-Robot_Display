# 基于ROS的仓储管理机器人设计与Webots平台仿真实现

## 场景设定

场景设定为物流仓库，包含四个柜子，两个自动导引车（AGV, Automated Guided Vehicle）机器人，八种货物，两个分拣机械臂，一个传送带，一个补货区(存放待上架货物)。

任务设定如下:

- 补货机器人(LoadGoodBot)在四个货架间巡回，若发现当前货架缺货，则去补货区寻找所需物品，拿起物品回到货架补货

- 取货机器人(FetchGoodBot)初始在指定位置待机，在键盘发出取某物品的指令后，前往对应货架取货，并将货物送往传送带分拣

- 分拣机械臂抓住传送带送来货物，放置到篮子中

## 效果展示

![](https://raw.githubusercontent.com/epcm/AI-Robot_Display/blob/main/upload.gif)
机器人移动到货架前观察点，摄像头识别空货架位置与所需货物，前往货物暂存区，寻找所需货物，对准并抓紧货物，回到货架，将物品放上货架，前往下一个货架

![](https://raw.githubusercontent.com/epcm/AI-Robot_Display/blob/main/fetch.gif)
等待键盘指令，接到“取红罐子”指令，前往对应货架，摄像头识别所需货物位置，对准并抓紧货物，前往出货位置，将货物放置在传送带，机械臂抓取货物放入箱子


![](https://raw.githubusercontent.com/epcm/AI-Robot_Display/blob/main/navigate.gif)
每个货架前设置一个巡航点，小车依次在四个巡航点之间移动，左侧为rviz可视化的小车路线以及坐标，背景为加载的由slam得到的地图，地图中间两个位置相近的坐标系为map和odom坐标系，运动的为小车坐标系，可以代表小车所在的相对位置，绿色的轨迹为要抵达目标所规划的行进路线。

## Progress

2022-1-13
1. FetchGoodBot逻辑可用(但是走路不避障，需要手动移开障碍；需要先点一下画面再按键'B'或'C'以指定所取货物)
2. 摩擦力调参(worldinfo-contactproperties)，LoadGoodBot调参(double Grasp_dis_set[], double grasp_force_threshold)，更好抓住物体
3. 缩短传送带，调整传送带朝向
4. 两车均增加激光雷达，雷达使用自定义的proto(./protos/SickLms291.proto)
## 致谢

本项目基于sszxc的项目[GitHub - sszxc/Super_Transbot: Supermarket Transport Robot](https://github.com/sszxc/Super_Transbot)
