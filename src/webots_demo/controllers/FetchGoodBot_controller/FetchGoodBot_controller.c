/*
 * SuperBot_Controller
 * ZXC and YYH
 * April, 2020
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <base.h>
#include <gripper.h>
#include <webots/keyboard.h>
#include <webots/robot.h>
#include <webots/motor.h>
#include <webots/touch_sensor.h>
#include <webots/camera.h>
#include <webots/camera_recognition_object.h>
#include <webots/gps.h>
#include <webots/compass.h>
#define TIME_STEP 32

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))
#define abs(a) (((a) < (0)) ? (0) : (a))
#define MAX_WIDTH 0.2f
#define MIN_WIDTH 0.0f
// WbDeviceTag motorL;
// WbDeviceTag motorR;
WbDeviceTag forceL;
WbDeviceTag forceR;
#define MAX_HEIGHT 0.4f
#define MIN_HEIGHT 0.03f
// WbDeviceTag motorM;
#define GRIPPER_MOTOR_MAX_SPEED 0.1
#define PI 3.1415926535f
static WbDeviceTag gripper_motors[3];
static WbDeviceTag camera[2];
WbDeviceTag gps;
WbDeviceTag compass;
double gps_values[2];         // gps值
double compass_angle;         //罗盘角度
double initial_posture[3];    //起点位姿,0为x,1为z,2为角度，每段轨迹只设置一次
double tmp_target_posture[3]; //临时目标位姿，需要不断计算
double fin_target_posture[3]; //最终目标位姿
double *recent_posture;       //巡回的起始点

int TargetIndex = 0; //当前关注的货架空位
int TargetGood;      //当前关注的货物种类
int Item_Grasped_Id = -1;
double load_target_posture[3]; //上货点

char *GoodsList[] = {"can", "cereal box", "cereal box red", "jam jar", "honey jar", "water bottle", "biscuit box", "red can", "beer bottle"};
//抓取时前探的距离，绝对值越小，前探越前
double Grasp_dis_set[] = {-0.16, -0.18, -0.18, -0.16, -0.16, -0.16, -0.16, -0.16, -0.16};
//寻找货物定点 右->...-> 上->...->左->...->下
int Travel_Point_Index = 0; //定点编号
int travel_points_sum = 0;  //走过的定点数量
double fixed_posture_travelaround[12][3] =
    {
        {1.05, 0.00, PI * 2},      //右
        {1.05, -1.05, PI * 2},     //右上
        {1.05, -1.05, PI / 2},     //右上 转
        {0.00, -1.05, PI / 2},     //上
        {-1.05, -1.05, PI / 2},    //左上
        {-1.05, -1.05, PI},        //左上 转
        {-1.05, 0, PI},            //左
        {-1.05, 1.05, PI},         //左下
        {-1.05, 1.05, 3 * PI / 2}, //左下 转
        {0.00, 1.05, 3 * PI / 2},  //下
        {1.05, 1.05, 3 * PI / 2},  //右下
        {1.05, 1.05, PI * 2}       //右下 转
};
//识别空货架定点 右->上->左->下
int CurrentShelf = 2; //当前货架编号 起点出发 逆时针
double fixed_posture[4][3] =
    {
        {0.8, -2, PI},  // 左上
        {-2.2, -2, PI}, // 左下
        {-2.2, 2, PI},  // 右下
        {0.8, 2, PI},   // 右上
};
double fixed_posture_findempty[4][3] =
    {
        {0.8, -2, PI},  // 左上
        {-2.2, -2, PI}, // 左下
        {-2.2, 2, PI},  // 右下
        {0.8, 2, PI},   // 右上
};
double fixed_posture_loadItem[4][3] =
    {
        {0.8, -2, PI},  // 左上
        {-2.2, -2, PI}, // 左下
        {-2.2, 2, PI},  // 右下
        {0.8, 2, PI},   // 右上
};

//机器人状态枚举
enum RobotState
{
  WaitIns,
  MovingToCabinet,
  GotoDropPlace,
  RecognizeFull,
  GrabItem,
  DropItem
};

double width = 0.0;  //爪子0~0.1
double height = 0.0; //爪子-0.05~0.45

static void step();
static void passive_wait(double sec);
static void display_helper_message();
void lift(double position);
void moveFingers(double position);
void init_all();
int keyboard_control(int c, enum RobotState *main_state);
void caculate_tmp_target(double tmp_posture[], double fin_posture[]);
void set_posture(double posture[], double x, double z, double angle);
void get_gps_values(double v_gps[]);
double vector2_angle(const double v1[], const double v2[]);
void get_compass_angle(double *ret_angle);
bool targetdist_reached(double target_posture[], double dist_threshold);
bool targetpos_reached(double target_posture[], double pos_threshold);
int name2index(char *name);
char *index2name(int index);

bool Find_Empty(WbDeviceTag camera);
bool Find_Goods(WbDeviceTag camera, char *good_name, int *item_grasped_id);
bool Aim_and_Grasp(int *grasp_state, WbDeviceTag camera, int objectID);
bool Moveto_CertainPoint(double fin_posture[], double reach_precision);
void Robot_State_Machine(enum RobotState *main_state, int *grasp_state);

//*?                 main函数      <开始>            ?*//
//主函数

int main(int argc, char **argv)
{
  init_all();

  printf("Ready to go!\n");
  enum RobotState main_state = WaitIns; //机器人运行状态
  int grasp_state = 0;                  //手爪状态
  while (true)
  {
    step();
    Robot_State_Machine(&main_state, &grasp_state);
    keyboard_control(wb_keyboard_get_key(), &main_state);
  }

  wb_robot_cleanup();

  return 0;
}
//*?                 main函数       <结束>            ?*//

//*?                 核心控制函数    <开始>            ?*//
//各模块初始化
void init_all()
{
  // 机器人初始化
  wb_robot_init();
  base_init();
  passive_wait(2.0);

  camera[0] = wb_robot_get_device("camera_top"); //相机初始化
  camera[1] = wb_robot_get_device("camera_front");
  wb_camera_enable(camera[0], TIME_STEP);
  wb_camera_recognition_enable(camera[0], TIME_STEP);
  wb_camera_enable(camera[1], TIME_STEP);
  wb_camera_recognition_enable(camera[1], TIME_STEP);

  // GPS初始化
  gps = wb_robot_get_device("gps");
  wb_gps_enable(gps, TIME_STEP);
  // Compass初始化
  compass = wb_robot_get_device("compass");
  wb_compass_enable(compass, TIME_STEP);
  //底盘全方位移动初始化
  base_goto_init(TIME_STEP);
  //设置初始位姿
  step();
  get_gps_values(gps_values);
  get_compass_angle(&compass_angle);
  set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
  //设置第一个定点位姿
  set_posture(fin_target_posture, fixed_posture_findempty[CurrentShelf][0], fixed_posture_findempty[CurrentShelf][1], fixed_posture_findempty[CurrentShelf][2]);
  recent_posture = fixed_posture_findempty[CurrentShelf];
  // set_posture(recent_posture, fin_target_posture[0], fin_target_posture[1], fin_target_posture[2]);
  //计算下一个临时目标;
  caculate_tmp_target(tmp_target_posture, fin_target_posture);
  //设置底盘运动目标
  base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);

  display_helper_message();
  wb_keyboard_enable(TIME_STEP);

  gripper_motors[0] = wb_robot_get_device("lift motor");
  gripper_motors[1] = wb_robot_get_device("left finger motor");
  gripper_motors[2] = wb_robot_get_device("right finger motor");

  //电机加力反馈
  wb_motor_enable_force_feedback(gripper_motors[1], 1);
  wb_motor_enable_force_feedback(gripper_motors[2], 1);
}

//机器人状态机
void Robot_State_Machine(enum RobotState *main_state, int *grasp_state)
{
  switch (*main_state)
  {
  case WaitIns:
  {
    set_posture(fin_target_posture, -1, 0, PI / 2);
    if (Moveto_CertainPoint(fin_target_posture, 0.05))
    {}
    break;
  }

  case MovingToCabinet:
  {
    // set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
    set_posture(fin_target_posture, fixed_posture[CurrentShelf][0], fixed_posture[CurrentShelf][1], fixed_posture[CurrentShelf][2]);
    if (Moveto_CertainPoint(fin_target_posture, 0.01))
    {
      *main_state = RecognizeFull;
      printf("main_state changes from MovingToCabinet to RecognizeFull!\n");
    }
    break;
  }
  case GotoDropPlace:
  {
    set_posture(fin_target_posture, -1, 0, PI / 2);
    if (Moveto_CertainPoint(fin_target_posture, 0.05))
    {
      *main_state = SearchItem;
      printf("main_state changes from GotoDropPlace to DropItem!\n");
    }
    break;
  }
  case RecognizeFull:
  {
    if (Find_Full(camera[0])) //货架上有空位
    {
      set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
      set_posture(fin_target_posture, -1, 0, PI / 2);
      *main_state = GotoDropPlace;
      printf("main_state changes from RecognizeFull to GotoDropPlace!\n");
    }
    else //货架上无空位
    {
      *main_state = WaitIns; //回到初始态
      printf("main_state changes from RecognizeFull to WaitIns!\n");
    }
    break;
  }
  case GrabItem:
  {
    if (Aim_and_Grasp(grasp_state, camera[1], Item_Grasped_Id))
    {
      printf("抓到回去啦!\n");
      get_gps_values(gps_values);
      get_compass_angle(&compass_angle);
      double load_x = 0;
      double load_z = 0.3; //向后走两步
      load_target_posture[0] = gps_values[0] - sin(compass_angle) * load_x + cos(compass_angle) * load_z;
      load_target_posture[1] = gps_values[1] - cos(compass_angle) * load_x - sin(compass_angle) * load_z;
      load_target_posture[2] = compass_angle;
      while (!Moveto_CertainPoint(load_target_posture, 0.01))
      {
        step(); //时序乱了
      }
      *main_state = BackMoving;
      printf("main_state changes from GrabItem to BackMoving!\n");
      *grasp_state = 0;
    }
    break;
  }

  case BackMoving:
  {
    if (Moveto_CertainPoint(fin_target_posture, 0.05))
    {
      if (fin_target_posture[0] == fixed_posture_findempty[CurrentShelf][0] && fin_target_posture[1] == fixed_posture_findempty[CurrentShelf][1])
      {
        *main_state = LoadItem;
        printf("main_state changes from BackMoving to LoadItem!\n");
        printf("到上货的地方了！\n");
        // travel_points_sum = 0;
        //计算一次和上货点的相对位移
        get_gps_values(gps_values);
        get_compass_angle(&compass_angle);
        double load_x = (TargetIndex % 8) * 0.24 - 0.85;
        double load_z = -0.16; //两步走
        load_target_posture[0] = gps_values[0] - sin(compass_angle) * load_x + cos(compass_angle) * load_z;
        load_target_posture[1] = gps_values[1] - cos(compass_angle) * load_x - sin(compass_angle) * load_z;
        load_target_posture[2] = compass_angle;
        printf("该上货了！\n");
        printf("GoodsonShelf[%d][%d] need %s\n", CurrentShelf, TargetIndex, index2name(TargetGood));
      }
      else
      {
        set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
        set_posture(fin_target_posture, fixed_posture_loadItem[CurrentShelf][0], fixed_posture_loadItem[CurrentShelf][1], fixed_posture_loadItem[CurrentShelf][2]);
      }
    }
    break;
  }
  case LoadItem:
  {
    get_gps_values(gps_values);
    get_compass_angle(&compass_angle);
    // printf("GPS device: %.3f %.3f\n", gps_values[0], gps_values[1]);
    if (Moveto_CertainPoint(load_target_posture, 0.001))
    {
      // printf("aim pos: x: %lf z: %lf angle: %lf\n", load_target_posture[0], load_target_posture[1], load_target_posture[2]);
      // printf("current pos: x: %lf z: %lf angle: %lf\n", gps_values[0], gps_values[1], compass_angle);
      double load_x_add = 0.5; //最后前进一些
      load_target_posture[0] = gps_values[0] + load_x_add;
      load_target_posture[1] = gps_values[1];
      load_target_posture[2] = PI;
      // load_target_posture[0] = gps_values[0] + cos(compass_angle) * load_z_add;
      // load_target_posture[1] = gps_values[1] - sin(compass_angle) * load_z_add;
      // load_target_posture[2] = compass_angle;
      while (!Moveto_CertainPoint(load_target_posture, 0.01))
      {
        step(); //时序乱了
      }
      base_reset();
      printf("小心上货！\n");
      wb_robot_step(50000 / TIME_STEP);
      moveFingers(width += 0.005);
      wb_robot_step(50000 / TIME_STEP);

      load_target_posture[0] = gps_values[0] - load_x_add;
      load_target_posture[1] = gps_values[1];
      load_target_posture[2] = PI;
      while (!Moveto_CertainPoint(load_target_posture, 0.01))
      {
        step(); //时序乱了
      }
      // load_target_posture[2] = (compass_angle > PI) ? compass_angle - PI : compass_angle + PI; //原地自转回来

      while (!Moveto_CertainPoint(load_target_posture, 0.01))
      {
        step(); //时序乱了
      }
      moveFingers(width = 0.0);
      lift(height = 0.020);
      *main_state = MovingAround;
      printf("main_state changes from LoadItem to MovingAround!\n");
      set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
      set_posture(fin_target_posture, recent_posture[0], recent_posture[1], recent_posture[2]);
    }
    break;
  }

  // ERROR
  default:
  {
    // printf("Error form State Machine : %d\n",*main_state);
    break;
  }
  }
}

//键盘控制基本运动
int keyboard_control(int c, enum RobotState *main_state)
{
  if ((c >= 0))
  { //&& c != pc) {//不要求键值变化
    switch (c)
    {
    case 'B':
    {
      recent_posture = fixed_posture_findempty[2];
      TargetGood = name2index("can");
      CurrentShelf = 2;
      printf("GoodsonShelf[%d][%d] need %s\n", CurrentShelf, TargetIndex, index2name(TargetGood));
      if (!strcmp("water bottle", objects[i].model)) //水杯特殊高度
        lift(height = 0.12);
      else if (!strcmp("cereal box red", objects[i].model)) //红大盒子特殊高度
        lift(height = 0.50);
      else if (!strcmp("cereal box", objects[i].model))
        lift(height = 0.30);
      else if (TargetIndex < 8)
        lift(height = 0.05);
      else if (CurrentShelf > 1) //矮柜0.23
        lift(height = 0.23);
      else if (CurrentShelf <= 1) //高柜0.43
        lift(height = 0.43);
      main_state = MovingToCabinet;
      printf("Go to Fetch Beer\n");
      break;
    }
    case 'C':
    {
      printf("Manually stopped！\n");
      *main_state = -1;
      break;
    }
    case WB_KEYBOARD_UP:
      printf("Go forwards\n");
      base_forwards();
      break;
    case WB_KEYBOARD_DOWN:
      printf("Go backwards\n");
      base_backwards();
      break;
    case WB_KEYBOARD_LEFT:
      printf("Strafe left\n");
      base_strafe_left();
      break;
    case WB_KEYBOARD_RIGHT:
      printf("Strafe right\n");
      base_strafe_right();
      break;
    case WB_KEYBOARD_PAGEUP:
      printf("Turn left\n");
      base_turn_left();
      break;
    case WB_KEYBOARD_PAGEDOWN:
      printf("Turn right\n");
      base_turn_right();
      break;
    case WB_KEYBOARD_END:
    case ' ':
      printf("Reset\n");
      base_reset();
      // arm_reset();
      break;
    case '+':
    case 388:
    case 65585:
      printf("Grip\n");
      //  gripper_grip();
      break;
    case '-':
    case 390:
      printf("Ungrip\n");
      //  gripper_release();
      break;
    case 332:
    case WB_KEYBOARD_UP | WB_KEYBOARD_SHIFT:
      // UpDownControll(Target_Height+=0.02);
      lift(height += 0.005);
      printf("Increase arm height to %.3f\n", height);
      break;
    case 326:
    case WB_KEYBOARD_DOWN | WB_KEYBOARD_SHIFT:
      // UpDownControll(Target_Height-=0.02);
      lift(height -= 0.005);
      printf("Decrease arm height to %.3f\n", height);
      // arm_decrease_height();
      break;
    case 330:
    case WB_KEYBOARD_RIGHT | WB_KEYBOARD_SHIFT:
      // ClawControll(Target_Width-=0.01);
      moveFingers(width -= 0.001);
      printf("Close the Claws to %.3f\n", width);
      break;
    case 328:
    case WB_KEYBOARD_LEFT | WB_KEYBOARD_SHIFT:
      // ClawControll(Target_Width+=0.01);
      moveFingers(width += 0.001);
      printf("Open the Claws to %.3f\n", width);
      break;
    default:
      fprintf(stderr, "Wrong keyboard input\n");
      break;
    }
  }
  return 0;
}

// GPS运动到指定位姿，返回bool值反馈是否到达，默认精度0.05
bool Moveto_CertainPoint(double fin_posture[], double reach_precision)
{
  if (targetdist_reached(fin_posture, reach_precision) && targetpos_reached(fin_posture, reach_precision))
  {
    // printf("到达目标位置！\n");
    // base_reset();
    return true;
  }
  else
  {
    caculate_tmp_target(tmp_target_posture, fin_posture);
    base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    // printf("Target:%s\n", point_name[point_index]);
    // printf("initial target： %.3f  %.3f  %.3f\n", initial_posture[0], initial_posture[1], initial_posture[2]);
    // printf("tmp target： %.3f  %.3f  %.3f\n", tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    // printf("final target： %.3f  %.3f  %.3f\n\n", fin_posture[0], fin_posture[1], fin_posture[2]);

    base_goto_run();
    return false;
  }
}

//前部摄像头校准并抓取
bool Aim_and_Grasp(int *grasp_state, WbDeviceTag camera, int objectID)
{
  //饼干盒ID43 水瓶ID56
  int number_of_objects = wb_camera_recognition_get_number_of_objects(camera);
  const WbCameraRecognitionObject *objects = wb_camera_recognition_get_objects(camera);
  for (int i = 0; i < number_of_objects; ++i)
  {
    if (objects[i].id == objectID) //找到画面中第一个ID物体
    {
      if (*grasp_state == 0) //调整位置
      {
        //大盒子特别提高一点抓，防止倾倒
        if (!strcmp("cereal box red", objects[i].model) || !strcmp("cereal box", objects[i].model))
          lift(height = 0.05);
        //水瓶特别提高一点
        else if (!strcmp("water bottle", objects[i].model))
          lift(height = 0.10);
        else
          lift(height = 0.0);
        moveFingers(width = objects[i].size[0] / 1.5);
        // printf("ID %d 的物体 %s 在 %lf %lf\n", objects[i].id, objects[i].model, objects[i].position[0], objects[i].position[2]);
        get_gps_values(gps_values);
        get_compass_angle(&compass_angle);
        double grasp_target_posture[3];

        double grasp_dis_set = Grasp_dis_set[name2index(objects[i].model)];
        // printf("抓取距离:%.3f\n",grasp_dis_set);
        //相对偏移 同时纵向位移稍微削弱一下
        grasp_target_posture[0] = gps_values[0] - sin(compass_angle) * objects[i].position[0] + cos(compass_angle) * (objects[i].position[2] - grasp_dis_set) * 0.6;
        grasp_target_posture[1] = gps_values[1] - cos(compass_angle) * objects[i].position[0] - sin(compass_angle) * (objects[i].position[2] - grasp_dis_set) * 0.6;
        grasp_target_posture[2] = compass_angle;

        Moveto_CertainPoint(grasp_target_posture, 0.05);

        double grasp_threshold = 0.02;
        if (fabs(objects[i].position[0]) < grasp_threshold && fabs(objects[i].position[2] - grasp_dis_set) < grasp_threshold)
        {
          *grasp_state += 1;
          printf("对准了！\n");
          base_reset();
          // 用视觉先来个抓手基本值
          printf("物体大小: %lf %lf\n", objects[i].size[0], objects[i].size[1]);
          moveFingers(width = objects[i].size[0] / 2);
          wb_robot_step(30000 / TIME_STEP);
        }
      }
      else if (*grasp_state == 1) //抓
      {
        double grasp_force_threshold = 40.0;
        if (wb_motor_get_force_feedback(gripper_motors[1]) > -grasp_force_threshold)
          moveFingers(width -= 0.0003); //步进
        else
        {
          printf("当前电机力反馈：%.3f\n", wb_motor_get_force_feedback(gripper_motors[1]));
          printf("抓紧了！\n");
          wb_robot_step(30000 / TIME_STEP); //等他抓稳定
          if (wb_motor_get_force_feedback(gripper_motors[1]) <= -grasp_force_threshold)
          {
            printf("爪宽：%.4f\n", width);
            *grasp_state += 1;
            printf("GoodsonShelf[%d][%d] need %s\n", CurrentShelf, TargetIndex, index2name(TargetGood));
            if (!strcmp("water bottle", objects[i].model)) //水杯特殊高度
              lift(height = 0.12);
            else if (!strcmp("cereal box red", objects[i].model)) //红大盒子特殊高度
              lift(height = 0.50);
            else if (!strcmp("cereal box", objects[i].model))
              lift(height = 0.30);
            else if (TargetIndex < 8)
              lift(height = 0.05);
            else if (CurrentShelf > 1) //矮柜0.23
              lift(height = 0.23);
            else if (CurrentShelf <= 1) //高柜0.43
              lift(height = 0.43);
            printf("举起了！height = %.3f\n", height);
            wb_robot_step(10000 / TIME_STEP);
          }
        }
      }
      else if (*grasp_state == 2) //举
      {
        return true;
      }
      break;
    }
  }
  return false;
}

//寻找空货架 给四个定点GPS 摄像头看四面墙 返回货架位置和一个商品种类
bool Find_Full(WbDeviceTag camera)
{
  int number_of_objects = wb_camera_recognition_get_number_of_objects(camera);
  printf("识别到 %d 个物体.\n", number_of_objects);
  const WbCameraRecognitionObject *objects = wb_camera_recognition_get_objects(camera);
  int GoodsonShelf[4][16]; //货架上的物品ID号 先下后上 先左后右
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 16; j++)
      GoodsonShelf[i][j] = -1; //不知道为啥写在定义的时候不成功
  for (int i = 0; i < number_of_objects; ++i)
  {
    // printf("物体 %d 的类型: %s ", i, objects[i].model);
    // printf("ID: %d ", objects[i].id);
    // printf("相对位置: %lf %lf %lf ", objects[i].position[0], objects[i].position[1],
    //        objects[i].position[2]);

    // printf("物体 %d 的相对姿态: %lf %lf %lf %lf\n", i, objects[i].orientation[0], objects[i].orientation[1],
    //        objects[i].orientation[2], objects[i].orientation[3]);
    // printf("物体的大小 %d: %lf %lf\n", i, objects[i].size[0], objects[i].size[1]);
    // printf("物体 %d 在图像中的坐标: %d %d\n", i, objects[i].position_on_image[0],
    //        objects[i].position_on_image[1]);
    // printf("物体 %d 在图像中的大小: %d %d\n", i, objects[i].size_on_image[0], objects[i].size_on_image[1]);
    // for (int j = 0; j < objects[i].number_of_colors; ++j)
    //   printf("颜色 %d/%d: %lf %lf %lf\n", j + 1, objects[i].number_of_colors, objects[i].colors[3 * j],
    //           objects[i].colors[3 * j + 1], objects[i].colors[3 * j + 2]);
    if (objects[i].position[2] < -3)
      continue;
    int Shelfx = max(0, floor((objects[i].position[0] + 0.84) * 4.17 + 0.5)); //左右 平均间隔0.24（架子宽度0.25）右移后对应一个系数 四舍五入
    int Shelfy = (objects[i].position[1] < -0.2) ? 0 : 1;                     //上下层 -0.20  为上下分界

    GoodsonShelf[CurrentShelf][Shelfy * 8 + Shelfx] = name2index(objects[i].model);
    printf("物体 %s 对应编号 %d 写入[%d] 写入编号为%d\n", objects[i].model, name2index(objects[i].model), Shelfy * 8 + Shelfx, GoodsonShelf[CurrentShelf][Shelfy * 8 + Shelfx]);
  }

  //检测完毕 判断下一个要取的货物类型
  int Full_Flag = 0;

  for (int j = 0; j < 16; j++)
    printf("GoodsonShelf[%d][%d] = %s\n", CurrentShelf, j, index2name(GoodsonShelf[CurrentShelf][j]));
  for (int j = 0; j < 16; j++)
  {
    // if (strlen(GoodsonShelf[CurrentShelf][j]) == 0) // 这种判断可能会crash
    //   Empty_Flag = 1;
    if (GoodsonShelf[CurrentShelf][j] == TargetGood)
    {
      Full_Flag = 1;
      TargetIndex = j;
      //如果整排都没有可能会出错 下次一定
      printf("GoodsonShelf[%d][%d] fetch %s\n", CurrentShelf, j, index2name(TargetGood));
      break;
    }
  }
  if (Full_Flag)
    return true;
  else
    return false;
}

//给一个固定的巡逻轨迹 前部摄像头寻找指定商品 靠近直到顶部摄像头能捕捉
bool Find_Goods(WbDeviceTag camera, char *good_name, int *item_grasped_id)
{
  int number_of_objects = wb_camera_recognition_get_number_of_objects(camera);
  const WbCameraRecognitionObject *objects = wb_camera_recognition_get_objects(camera);
  double grasp_dis_threshold = -0.5;
  for (int i = 0; i < number_of_objects; ++i)
  {
    // printf("goodname:%s 距离 %s 有 %.3f m \n", good_name, objects[i].model, -objects[i].position[2]);
    if (strcmp(objects[i].model, good_name) == 0)
    {
      if (objects[i].position[2] > 1.3 * grasp_dis_threshold)
      {
        //  //距离近、左右位置对、且是侧面
        if (objects[i].position[2] > grasp_dis_threshold && fabs(objects[i].position[0]) < 0.1 && objects[i].size[0] <= 0.15)
        {
          // printf("找到了离我%.3f m 的 %s\n", -objects[i].position[2], good_name);
          *item_grasped_id = objects[i].id;
          return true;
        }
      }
      printf("Too far away\n");
    }
  }
  return false;
}

//*?                 核心控制函数    <结束>               ?*//

//*?                 功能函数        <开始>               ?*//
//仿真前进 1 step
static void step()
{
  if (wb_robot_step(TIME_STEP) == -1)
  {
    wb_robot_cleanup();
    exit(EXIT_SUCCESS);
  }
}

//软件仿真延时
static void passive_wait(double sec)
{
  double start_time = wb_robot_get_time();
  do
  {
    step();
  } while (start_time + sec > wb_robot_get_time());
}

//打印帮助
static void display_helper_message()
{
  printf("FetchBot Start");
  return;
}

//设置机械臂上升高度
void lift(double position)
{
  wb_motor_set_velocity(gripper_motors[0], GRIPPER_MOTOR_MAX_SPEED);
  wb_motor_set_position(gripper_motors[0], position);
}

//设置手爪开合大小
void moveFingers(double position)
{
  wb_motor_set_velocity(gripper_motors[1], GRIPPER_MOTOR_MAX_SPEED);
  wb_motor_set_velocity(gripper_motors[2], GRIPPER_MOTOR_MAX_SPEED);
  wb_motor_set_position(gripper_motors[1], position);
  wb_motor_set_position(gripper_motors[2], position);
}

//细分目标位姿
double SUB = 2.0; //细分目标份数
void caculate_tmp_target(double tmp_posture[], double fin_posture[])
{
  get_gps_values(gps_values);
  get_compass_angle(&compass_angle);
  tmp_posture[0] = gps_values[0] + (fin_posture[0] - gps_values[0]) / SUB;
  tmp_posture[1] = gps_values[1] + (fin_posture[1] - gps_values[1]) / SUB;
  //选择所需旋转角度最小的的方向进行旋转
  if (fabs(fin_posture[2] - compass_angle) > PI)
  {
    tmp_posture[2] = compass_angle + (compass_angle - fin_posture[2]) / (SUB * 5);
  }
  else
    tmp_posture[2] = compass_angle + (fin_posture[2] - compass_angle) / (SUB * 5);
}

//设置位姿
void set_posture(double posture[], double x, double z, double angle)
{
  posture[0] = x;
  posture[1] = z;
  posture[2] = angle;
}

// bool函数 返回是否到达指定位置
bool targetdist_reached(double target_posture[], double dist_threshold)
{
  get_gps_values(gps_values);
  double dis = sqrt((gps_values[0] - target_posture[0]) * (gps_values[0] - target_posture[0]) + (gps_values[1] - target_posture[1]) * (gps_values[1] - target_posture[1]));

  // double angle = compass_angle - target_posture[2];
  if (dis <= dist_threshold)
  {
    return true;
  }
  else
  {
    // printf("距离目标位置：%.3f  m\n", dis);
    return false;
  }
}

// bool函数 返回是否到达指定姿态
bool targetpos_reached(double target_posture[], double pos_threshold)
{
  get_compass_angle(&compass_angle);
  double angle = target_posture[2] - compass_angle;
  if (fabs(angle) <= pos_threshold || fabs(angle) >= 2 * PI - pos_threshold)
    return true;
  return false;
}

//获取GPS的值
void get_gps_values(double v_gps[])
{
  const double *gps_raw_values = wb_gps_get_values(gps);
  v_gps[0] = gps_raw_values[0];
  v_gps[1] = gps_raw_values[2];
}

//数学函数，返回arctan值
double vector2_angle(const double v1[], const double v2[])
{
  return atan2(v2[1], v2[0]) - atan2(v1[1], v1[0]);
}

//计算罗盘角度
void get_compass_angle(double *ret_angle)
{
  const double *compass_raw_values = wb_compass_get_values(compass);
  const double v_front[2] = {compass_raw_values[0], compass_raw_values[1]};
  const double v_north[2] = {1.0, 0.0};
  *ret_angle = vector2_angle(v_front, v_north) + PI; // angle E(0, 2*PI)
  // printf("当前姿态：%.3f  rad\n", *ret_angle);
}

//商品名转换
int name2index(const char *name)
{
  for (int i = 0; i < sizeof(GoodsList); i++)
  {
    // printf(" %s : %s \n", name, GoodsList[i]);
    // if (name==GoodsList[i])
    if (strcmp(name, GoodsList[i]) == 0)
      return i;
  }
  return -1;
}

//商品名转换
char *index2name(int index)
{
  return GoodsList[index];
}

//*?                 功能函数        <结束>               ?*//