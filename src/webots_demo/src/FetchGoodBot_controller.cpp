/*
 * SuperBot_Controller
 * ZXC and YYH
 * April, 2020
 */
#include <ros/ros.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <std_msgs/String.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/LaserScan.h>
#include <sensor_msgs/NavSatFix.h>
#include <geometry_msgs/PointStamped.h>
#include <sensor_msgs/MagneticField.h>
#include <tf/transform_broadcaster.h>
#include <webots_ros/get_int.h>
#include <webots_ros/get_bool.h>
#include <webots_ros/set_string.h>
#include <webots_ros/StringStamped.h>
#include <webots_ros/Float64Stamped.h>

#include <webots_ros/set_float.h>
#include <webots_ros/set_int.h>
#include <webots_ros/Int32Stamped.h>

#define TIME_STEP 32

// stimulus coefficients
#define K1 3.0
#define K2 10.0
#define K3 10.0
#define DISTANCE_TOLERANCE 0.05
#define ANGLE_TOLERANCE 0.05

#define max(a, b) (((a) > (b)) ? (a) : (b))
#define min(a, b) (((a) > (b)) ? (b) : (a))
#define abs(a) (((a) < (0)) ? (0) : (a))
#define MAX_WIDTH 0.2f
#define MIN_WIDTH 0.0f
// WbDeviceTag motorL;
// WbDeviceTag motorR;
#define MAX_HEIGHT 0.4f
#define MIN_HEIGHT 0.03f
#define MAX_SPEED 12
#define NMOTORS 4
#define NGRIPPER 3
#define NCAMERA 2

#define GRIPPER_MOTOR_MAX_SPEED 0.1
#define PI 3.1415926535f

///////////Recognition//////////

typedef struct myObject
{
  int id;
  char model[20];
  double position[3];
  double size[2];
} myObject;

typedef struct RecognitionInfo
{
  int number_of_objects;
  myObject objects[30];
}RecognitionInfo;

///////////Recognition//////////

///////////math//////////
typedef struct
{
    double u;
    double v;
} Vector2;
typedef struct
{
    double u;
    double v;
    double w;
} Vector3;
typedef struct
{
    Vector3 a;
    Vector3 b;
    Vector3 c;
} Matrix33;
typedef struct
{
    Vector2 v_target;
    double alpha;
    bool reached;
} goto_struct;

// --- Vector2 functions ---
double vector2_norm(const Vector2 *v);                                // ||v||
void vector2_minus(Vector2 *v, const Vector2 *v1, const Vector2 *v2); // v = v1-v2
double vector2_angle(const Vector2 *v1, const Vector2 *v2);           // angle between v1 and v2 -> [0, 2Pi]
// --- Vector3 functions ---
void vector3_set_values(Vector3 *vect, double u, double v, double w);
// --- Matrix33 functions ---
void matrix33_set_values(Matrix33 *m, double au, double av, double aw, double bu, double bv, double bw, double cu, double cv, double cw);
void matrix33_set_identity(Matrix33 *m);
void matrix33_mult_vector3(Vector3 *res, const Matrix33 *m, const Vector3 *v); // res = m * v
double bound(double v, double a, double b);
void robot_step(int time);

///////////math//////////

double gps_values[2];         // gps值
double compass_angle;         //罗盘角度
double initial_posture[3];    //起点位姿,0为x,1为z,2为角度，每段轨迹只设置一次
double tmp_target_posture[3]; //临时目标位姿，需要不断计算
double fin_target_posture[3]; //最终目标位姿
double drop_place_posture[3]; //卸货的位姿
double *recent_posture;       //巡回的起始点

int TargetIndex = 0; //当前关注的货架空位
int TargetGood;      //当前关注的货物种类
int Item_Grasped_Id = -1;
double load_target_posture[3]; //上货点

bool reg_call_back = false;
bool finger_call_back = false;
bool gps_call_back = false;
bool compass_call_back = false;
bool keyboard_call_back = false;

char *GoodsList[] = {"can", "cereal box", "cereal box red", "jam jar", "honey jar", "water bottle", "biscuit box", "red can", "beer bottle"};
static const char *motorNames[NMOTORS] = {"wheel1", "wheel2", "wheel3", "wheel4"}; //匹配之前定义好的电机name
static const char *gripperNames[NGRIPPER] = {"lift_motor", "left_finger_motor", "right_finger_motor"};//匹配之前定义好的电机name
//抓取时前探的距离，绝对值越小，前探越前
double Grasp_dis_set[] = {-0.16, -0.18, -0.18, -0.16, -0.16, -0.16, -0.16, -0.16, -0.16};
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

std::string ROS_NODE_NAME = "fetch_good_bot";
ros::NodeHandle *n;
static int controllerCount;
static std::vector<std::string> controllerList;
ros::ServiceClient timeStepClient;
webots_ros::set_int timeStepSrv;
float baseSpeed;              //底盘速度
float motorPositions[4];      //四个轮子的位置
float motorSpeeds[4];         //四个轮子的速度
// float armSpeeds[5];           //五个关节的速度
static goto_struct goto_data; //目标位置
sensor_msgs::NavSatFix gpsRawValue;
sensor_msgs::MagneticField compassRawValue;
webots_ros::Float64Stamped gripperRawvalue[3];
webots_ros::Int32Stamped keyboardRawvalue;
struct RecognitionInfo recognitionRawvalue;

double width = 0.0;  //爪子0~0.1
double height = 0.0; //爪子-0.05~0.45

void Update();
static void step();
static void passive_wait(double sec);
static void display_helper_message();
void lift(double position);
void moveFingers(double position);
int keyboard_control(int c, enum RobotState *main_state);
void caculate_tmp_target(double tmp_posture[], double fin_posture[]);
bool MoveShortDistance(double fin_posture[], double reach_precision);
void set_posture(double posture[], double x, double z, double angle);
void get_gps_values(double v_gps[]);
double vector2_angle(const double v1[], const double v2[]);
void get_compass_angle(double *ret_angle);
bool targetdist_reached(double target_posture[], double dist_threshold);
bool targetpos_reached(double target_posture[], double pos_threshold);
int name2index(char *name);
char *index2name(int index);

bool Find_Full(char *good_name, int *item_grasped_id);
// bool Find_Goods(WbDeviceTag camera, char *good_name, int *item_grasped_id);
bool Aim_and_Grasp(int *grasp_state, int objectID);
bool Moveto_CertainPoint(double fin_posture[], double reach_precision);
void Robot_State_Machine(enum RobotState *main_state, int *grasp_state);

bool Init(int argc, char **argv);
bool InitController(int argc, char **argv);
bool InitMotor();
bool InitGps();
bool InitCompass();
bool InitCamera();
bool InitGripper();
bool InitReceiver();
bool InitKeyboard();

bool SetMotorPosition();
bool SetMotorSpeed();
bool set_gripper_velocity(int index, float speed);
bool set_gripper_position(int index, float position);

void base_reset();
void base_forwards();
void base_backwards();
void base_turn_left();
void base_turn_right();
void base_strafe_left();
void base_strafe_right();

void base_goto_init();
void base_goto_set_target(double x, double z, double a);
void base_goto_run();
bool base_goto_reached();

void GpsCallback(const sensor_msgs::NavSatFix::ConstPtr &msg);
void CompassCallback(const sensor_msgs::MagneticField::ConstPtr &msg);
void ControllerNameCallback(const std_msgs::String::ConstPtr &name);
void left_finger_Callback(const webots_ros::Float64Stamped::ConstPtr &msg);
void receiverCallback(const webots_ros::StringStamped::ConstPtr &msg);
void keyboardCallback(const webots_ros::Int32Stamped::ConstPtr &msg);

void robot_step(int time);
void Quit(int sig);

int main(int argc, char **argv)
{
  setlocale(LC_ALL, ""); // 用于显示中文字符

  if(Init(argc, argv)) ROS_INFO("Init succeed!");
  else ROS_ERROR("Init failed!");

  //创建话题订阅者
  ros::Subscriber gpsSub = n->subscribe(ROS_NODE_NAME+"/gps/values", 10, GpsCallback);
  ros::Subscriber compassSub = n->subscribe(ROS_NODE_NAME+"/compass/values", 10, CompassCallback);
  ros::Subscriber keyboardSub = n->subscribe(ROS_NODE_NAME+"/keyboard/key", 10, keyboardCallback);

  ROS_INFO("Ready to go! Wait for your instruction\n");
  ROS_INFO("Press 'C' to fetch soda can\n");
  ROS_INFO("Press 'B' to fetch water bottle\n");
  enum RobotState main_state = WaitIns; //机器人运行状态
  int grasp_state = 0;                  //手爪状态
  while (ros::ok())
  {
    step();
    ros::spinOnce();
    Robot_State_Machine(&main_state, &grasp_state);
    if(!keyboard_call_back){
      keyboardRawvalue.data = -1;
    }
    keyboard_control(keyboardRawvalue.data, &main_state);
    keyboard_call_back = false;
  }

  timeStepSrv.request.value = 0;
  timeStepClient.call(timeStepSrv);
  ros::shutdown();
  return 0;
}

bool Init(int argc, char **argv) //初始化函数
{
    base_goto_init();
    if (InitController(argc, argv) == true) //控制器初始化
      ROS_INFO("InitController succeed!");
    else
    {
      ROS_ERROR("InitController failed!");
      return false;
    }
    passive_wait(2.0); //仿真延时
    if (InitMotor() == true) //电机初始化
      ROS_INFO("InitMotor succeed!");
    else
    {
      ROS_ERROR("InitMotor failed!");
      return false;
    }
    if (InitGripper() == true) //电机初始化
      ROS_INFO("InitGripper succeed!");
    else
    {
      ROS_ERROR("InitGripper failed!");
      return false;
    }
    //传感器初始化
    if (InitGps() == true) //Gps初始化
      ROS_INFO("InitGps succeed!");
    else
    {
      ROS_ERROR("InitGps failed!");
      return false;
    }
    if (InitCompass() == true) //电子罗盘初始化
      ROS_INFO("InitCompass succeed!");
    else
    {
      ROS_ERROR("InitCompass failed!");
      return false;
    }
    if (InitReceiver() == true) //控制器初始化
      ROS_INFO("InitReceiver succeed!");
    else
    {
      ROS_ERROR("InitReceiver failed!");
      return false;
    }
    if (InitKeyboard() == true) //控制器初始化
      ROS_INFO("InitKeyboard succeed!");
    else
    {
      ROS_ERROR("InitKeyboard failed!");
      return false;
    }
    base_goto_init(); //导航位置初始化

  //设置初始位姿
  step();
  get_gps_values(gps_values);
  get_compass_angle(&compass_angle);
  set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
  set_posture(fin_target_posture, fixed_posture_findempty[CurrentShelf][0], fixed_posture_findempty[CurrentShelf][1], fixed_posture_findempty[CurrentShelf][2]);
  recent_posture = fixed_posture_findempty[CurrentShelf];
  //计算下一个临时目标;
  caculate_tmp_target(tmp_target_posture, fin_target_posture);
  //设置底盘运动目标
  base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);

  return true;
}

// 控制器初始化
bool InitController(int argc, char **argv)
{
    std::string controllerName;
    //创建节点ros"load"
    ros::init(argc, argv, ROS_NODE_NAME, ros::init_options::AnonymousName);
    n = new ros::NodeHandle;
    signal(SIGINT, Quit);
    //订阅话题model_name来查看当前可供操作的机器人
    ros::Subscriber nameSub = n->subscribe("model_name", 100, ControllerNameCallback);
    while (controllerCount == 0 || controllerCount < nameSub.getNumPublishers())
        ros::spinOnce();

    ros::spinOnce();
    //设定机器人的仿真步数
    timeStepClient = n->serviceClient<webots_ros::set_int>(ROS_NODE_NAME + "/robot/time_step");
    timeStepSrv.request.value = TIME_STEP;

    //如果多余一个机器人，选择该控制器的控制对象
    if (controllerCount == 1)
        controllerName = controllerList[0];
    else
    {
        int wantedController = 0;
        std::cout << "Choose the # of the controller you want touse:\n";
        std::cin >> wantedController;
        if (1 <= wantedController && wantedController <= controllerCount)
            controllerName = controllerList[wantedController - 1];
        else
        {
            ROS_ERROR("Invalid number for controller choice.");
            return false;
        }
    }
    ROS_INFO("Using controller: '%s'", controllerName.c_str());
    // 关闭该服务
    nameSub.shutdown();
    return true;
}

bool InitMotor() // 初始化电机
{
    for (int i = 0; i < NMOTORS; i++)
    {
        motorPositions[i] = INFINITY;
        motorSpeeds[i] = 0;
    }
    if (SetMotorPosition() && SetMotorSpeed())
        return true;
    else
        return false;
}

bool InitGripper() // 初始化抓取电机
{
    for (int i = 0; i < NGRIPPER; ++i)
    {
        ros::ServiceClient gripperClient;
        webots_ros::set_int gripperSrv;
        //请求服务 load_good_bot/xxx_motor/
        gripperClient = n->serviceClient<webots_ros::set_int>(ROS_NODE_NAME + std::string("/") + std::string(gripperNames[i]) + std::string("/force_feedback_sensor/enable"));
        gripperSrv.request.value = TIME_STEP;
        if (gripperClient.call(gripperSrv) && gripperSrv.response.success)
            ;
        else
        {
            ROS_ERROR("Failed enable gripper.");
            return false;
        }
    }
    ROS_INFO("succeed enable gripper.");
    return true;
}

bool InitGps() // 初始化GPS
{
    ros::ServiceClient enableClient;
    webots_ros::set_int enableSrv;
    //请求服务 load_good_bot/gps/enable
    enableClient = n->serviceClient<webots_ros::set_int>(std::string(ROS_NODE_NAME+"/gps/enable"));
    enableSrv.request.value = TIME_STEP;
    if (enableClient.call(enableSrv) && enableSrv.response.success)
        ROS_INFO("succeed enable gps.");
    else
    {
        ROS_ERROR("Failed to enable gps.");
        return false;
    }
    return true;
}

bool InitCompass() // 初始化Compass
{
    ros::ServiceClient enableClient;
    webots_ros::set_int enableSrv;
    //请求服务 load_good_bot/compass/enable
    enableClient = n->serviceClient<webots_ros::set_int>(ROS_NODE_NAME+std::string("/compass/enable"));
    enableSrv.request.value = TIME_STEP;
    if (enableClient.call(enableSrv) && enableSrv.response.success)
        ROS_INFO("succeed enable compass.");
    else
    {
        ROS_ERROR("Failed to enable compass.");
        return false;
    }
    return true;
}

bool InitReceiver() // 初始化接收器
{
  ros::ServiceClient enableClient;
    webots_ros::set_int enableSrv;
    //请求服务 load_good_bot/receiver/enable
    enableClient = n->serviceClient<webots_ros::set_int>(ROS_NODE_NAME+std::string("/receiver/enable"));
    enableSrv.request.value = TIME_STEP;
    if (enableClient.call(enableSrv) && enableSrv.response.success)
        ROS_INFO("succeed enable receiver.");
    else
    {
        ROS_ERROR("Failed to enable receiver.");
        return false;
    }
    return true;
}

bool InitKeyboard() // 初始化键盘
{
    ros::ServiceClient enableClient;
    webots_ros::set_int enableSrv;
    //请求服务 load_good_bot/receiver/enable
    enableClient = n->serviceClient<webots_ros::set_int>(ROS_NODE_NAME+std::string("/keyboard/enable"));
    enableSrv.request.value = TIME_STEP;
    if (enableClient.call(enableSrv) && enableSrv.response.success)
        ROS_INFO("succeed enable keyboard.");
    else
    {
        ROS_ERROR("Failed to enable keyboard.");
        return false;
    }
    return true;
}

// 机器人状态机
void Robot_State_Machine(enum RobotState *main_state, int *grasp_state)
{
  switch (*main_state)
  {
  // 到达特定位置，等待指令
  case WaitIns:
  {
    set_posture(fin_target_posture, 3.25, 1, 0);
    if (Moveto_CertainPoint(fin_target_posture, 0.1))
    {
    }
    break;
  }
  // 到达货物所在柜子
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
  // 寻找柜子上所需物品，并到达其前方
  case RecognizeFull:
  {
    if (Find_Full(index2name(TargetGood), &Item_Grasped_Id)) //货架上有货物
    {
      set_posture(initial_posture, gps_values[0], gps_values[1], compass_angle);
      get_gps_values(gps_values);
      get_compass_angle(&compass_angle);
      double load_x = (TargetIndex % 8) * 0.24 - 0.85;
      double load_z = -0.16;
      load_target_posture[0] = gps_values[0] - sin(compass_angle) * load_x + cos(compass_angle) * load_z;
      load_target_posture[1] = gps_values[1] - cos(compass_angle) * load_x - sin(compass_angle) * load_z;
      load_target_posture[2] = compass_angle;
      printf("Goto x:%lf z:%lf\n", load_target_posture[0], load_target_posture[1]);
      while (!MoveShortDistance(load_target_posture, 0.05))
      {
        step();
        ros::spinOnce();
      }
      *main_state = GrabItem;
      printf("main_state changes from RecognizeFull to GrabItem!\n");
    }
    else //货架上无空位
    {
      *main_state = WaitIns; //回到初始态
      printf("main_state changes from RecognizeFull to WaitIns!\n");
    }
    break;
  }
  // 调整位置并抓起物体，然后后退几步
  case GrabItem:
  {
    if (Aim_and_Grasp(grasp_state, Item_Grasped_Id))
    {
      printf("抓到回去啦!\n");
      lift(height = 0.1);
      get_gps_values(gps_values);
      get_compass_angle(&compass_angle);
      double load_x = 0;
      double load_z = 0.3; //向后走两步
      load_target_posture[0] = gps_values[0] - sin(compass_angle) * load_x + cos(compass_angle) * load_z;
      load_target_posture[1] = gps_values[1] - cos(compass_angle) * load_x - sin(compass_angle) * load_z;
      load_target_posture[2] = compass_angle;
      while (!MoveShortDistance(load_target_posture, 0.01))
      {
        step(); //时序乱了
        ros::spinOnce();
      }
      *main_state = GotoDropPlace;
      printf("main_state changes from GrabItem to GotoDropPlace!\n");
      *grasp_state = 0;
    }
    break;
  }
  // 前往传送带
  case GotoDropPlace:
  {
    set_posture(fin_target_posture, drop_place_posture[0], drop_place_posture[1], drop_place_posture[2]);
    if (Moveto_CertainPoint(fin_target_posture, 0.05))
    {
      *main_state = DropItem;
      printf("main_state changes from GotoDropPlace to DropItem!\n");
    }
    break;
  }
  // 卸货
  case DropItem:
  {
    get_gps_values(gps_values);
    get_compass_angle(&compass_angle);
    // printf("GPS device: %.3f %.3f\n", gps_values[0], gps_values[1]);
    if (MoveShortDistance(fin_target_posture, 0.01))
    {
      printf("小心上货!\n");
      robot_step(100 * TIME_STEP);
      moveFingers(width += 0.005);
      robot_step(100 * TIME_STEP);

      double load_x_add = 0.5; //最后前进一些
      load_target_posture[0] = gps_values[0] - load_x_add;
      load_target_posture[1] = gps_values[1];
      load_target_posture[2] = PI;
      while (!MoveShortDistance(load_target_posture, 0.01))
      {
        step();
        ros::spinOnce();
      }
      moveFingers(width = 0.0);
      lift(height = 0.020);
      *main_state = WaitIns;
      printf("main_state changes from DropItem to WaitIns!\n");
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
    case 'C':
    {
      recent_posture = fixed_posture_findempty[3];
      TargetGood = name2index("red can");
      CurrentShelf = 3;
      drop_place_posture[0] = 3.08;
      drop_place_posture[1] = 0.19;
      drop_place_posture[2] = PI;
      *main_state = MovingToCabinet;
      printf("Go to Fetch can\n");
      break;
    }
    case 'B':
    {
      TargetGood = name2index("water bottle");
      CurrentShelf = 1;
      recent_posture = fixed_posture_findempty[CurrentShelf];
      drop_place_posture[0] = 3.08;
      drop_place_posture[1] = 0.15;
      drop_place_posture[2] = PI;
      *main_state = MovingToCabinet;
      printf("Go to Fetch water bottle\n");
      break;
    }
    default:
      //fprintf(stderr, "Wrong keyboard input\n");
      break;
    }
  }
  return 0;
}

// GPS运动到指定位姿，返回bool值反馈是否到达，默认精度0.05
bool Moveto_CertainPoint(double fin_posture[], double reach_precision)
{ROS_INFO("move");
  if (targetdist_reached(fin_posture, reach_precision) && targetpos_reached(fin_posture, reach_precision))
  {
    // printf("到达目标位置！\n");
    // base_reset();
    return true;
  }
  else
  {
    caculate_tmp_target(tmp_target_posture, fin_posture);
    // API
    base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    base_goto_run();// API 应该用move_base实现？
    Update();
    return false;
  }
}

// 短距离移动
bool MoveShortDistance(double fin_posture[], double reach_precision)
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
    // API
    base_goto_set_target(tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    // printf("Target:%s\n", point_name[point_index]);
    // printf("initial target： %.3f  %.3f  %.3f\n", initial_posture[0], initial_posture[1], initial_posture[2]);
    // printf("tmp target： %.3f  %.3f  %.3f\n", tmp_target_posture[0], tmp_target_posture[1], tmp_target_posture[2]);
    // printf("final target： %.3f  %.3f  %.3f\n\n", fin_posture[0], fin_posture[1], fin_posture[2]);

    base_goto_run();// API 应该用move_base实现？
    Update();
    return false;
  }
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
  /*
  while(!gps_call_back){
    ros::spinOnce();
  }
  gps_call_back = false;
  */
  v_gps[0] = gpsRawValue.latitude;
  v_gps[1] = gpsRawValue.longitude;
}

//计算罗盘角度
void get_compass_angle(double *ret_angle)
{
  /*
  while(!compass_call_back){
    ros::spinOnce();
  }
  compass_call_back = false;
  */
  const Vector2 v_front = {compassRawValue.magnetic_field.x, compassRawValue.magnetic_field.y};
  const Vector2 v_right = {-v_front.v, v_front.u};
  const Vector2 v_north = {1.0, 0.0};
  *ret_angle = vector2_angle(&v_front, &v_north) + PI; // angle E(0, 2*PI)
  //ROS_INFO("compass_angle:%.2f",*ret_angle);
  //ROS_INFO("%.2f %.2f %.2f\n",compassRawValue.magnetic_field.x,compassRawValue.magnetic_field.y,compassRawValue.magnetic_field.z);
}

// 设定姿势
void set_posture(double posture[], double x, double z, double angle)
{
  posture[0] = x;
  posture[1] = z;
  posture[2] = angle;
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
    tmp_posture[2] = compass_angle + (compass_angle - fin_posture[2]) / (SUB * 5);
  else
    tmp_posture[2] = compass_angle + (fin_posture[2] - compass_angle) / (SUB * 5);
  //ROS_INFO("tmp_posture_angle:%.2f compass_angle:%.2f fin_posture[2]:%.2f",tmp_posture[2],compass_angle,fin_posture[2]);
}

//gps回调函数
void GpsCallback(const sensor_msgs::NavSatFix::ConstPtr &msg)
{
    gpsRawValue = *msg;
    gps_call_back = true;
    //ROS_INFO("Update gpsRawvalue: x %.2f z %.2f",gpsRawValue.latitude, gpsRawValue.longitude);
}

//compass回调函数
void CompassCallback(const sensor_msgs::MagneticField::ConstPtr &msg)
{
    compass_call_back = true;
    compassRawValue = *msg;
}

void ControllerNameCallback(const std_msgs::String::ConstPtr &name)
{
    controllerCount++;
    controllerList.push_back(name->data);
    ROS_INFO("Controller #%d: %s.", controllerCount, controllerList.back().c_str());
}

void left_finger_Callback(const webots_ros::Float64Stamped::ConstPtr &msg){
    gripperRawvalue[1] = *msg;
    finger_call_back = true;
}

void receiverCallback(const webots_ros::StringStamped::ConstPtr &msg){
  const char *p = msg->data.c_str();
  //ROS_INFO("GET MESSAGE: %s", p);
  // 将字符信息处理为recognitionRawvalue
  std::istringstream iss;
  iss.str(p);
  iss >> recognitionRawvalue.number_of_objects;
  iss.get();
  for(int i=0;i<recognitionRawvalue.number_of_objects;i++){
    iss.getline(recognitionRawvalue.objects[i].model, 20);
    char buffer[100];
    iss.getline(buffer, 100);
    sscanf(buffer, "%d %lf %lf %lf %lf %lf\n", &recognitionRawvalue.objects[i].id, &recognitionRawvalue.objects[i].position[0],
    &recognitionRawvalue.objects[i].position[1], &recognitionRawvalue.objects[i].position[2],
    &recognitionRawvalue.objects[i].size[0], &recognitionRawvalue.objects[i].size[1]);
    //iss>>recognitionRawvalue.objects[i].model>>recognitionRawvalue.objects[i].id;
    //iss>>recognitionRawvalue.objects[i].position[0]>>recognitionRawvalue.objects[i].position[1]>>recognitionRawvalue.objects[i].position[2];
    //iss>>recognitionRawvalue.objects[i].size[0]>>recognitionRawvalue.objects[i].size[1];
    //ROS_INFO("%s %f", recognitionRawvalue.objects[i].model, recognitionRawvalue.objects[i].size[0]);
  }
  reg_call_back = true;
}

void keyboardCallback(const webots_ros::Int32Stamped::ConstPtr &msg){
  keyboardRawvalue = *msg;
  keyboard_call_back = true;
}

// 视觉识别
void get_Recognition_info()
{
  webots_ros::get_int queue_length_srv;
  webots_ros::get_bool next_packet_srv;
  //webots_ros::set_string emitter_srv;
  queue_length_srv.request.ask = next_packet_srv.request.ask = 1;
  ros::ServiceClient queue_length_Client = n->serviceClient<webots_ros::get_int>(ROS_NODE_NAME + "/receiver/get_queue_length");
  ros::ServiceClient next_package_Client = n->serviceClient<webots_ros::get_bool>(ROS_NODE_NAME + "/receiver/next_packet");
  //ros::ServiceClient emitter_Client = n->serviceClient<webots_ros::get_bool>(ROS_NODE_NAME + "/emitter/send");
  ros::Subscriber receiver_sub = n->subscribe(ROS_NODE_NAME+"/receiver/data", 10, receiverCallback);
  //printf("start ask for service\n");
  //WbDeviceTag emitter = wb_robot_get_device("emitter");
  //struct RecognitionInfo *info = (struct RecognitionInfo *)malloc(sizeof(RecognitionInfo));
  // int flag = 1;
  while (queue_length_Client.call(queue_length_srv) && queue_length_srv.response.value > 0)
  {
    next_package_Client.call(next_packet_srv);
  }
  //std::__cxx11::string str = "sent";
  //emitter_srv.request.value = str;
  //emitter_Client.call(emitter_srv);
  //printf("sended\n");
  while (queue_length_Client.call(queue_length_srv) && queue_length_srv.response.value == 0)
  {
    step();
  }
  // wb_robot_step(5000 / TIME_STEP);
  queue_length_Client.call(queue_length_srv);
  //printf("start receive %d\n", queue_length_srv.response.value);
  while(!reg_call_back){
    step();
    ros::spinOnce();
    //ROS_INFO("waiting msg");
  }
  reg_call_back = false;
  //struct RecognitionInfo msg = recognitionRawvalue;
  //ROS_INFO("number:%d model: %s\n", msg.number_of_objects, msg.objects[0].model);
  return;
}

bool Find_Full(char *good_name, int *item_grasped_id)
{
  // int number_of_objects = wb_camera_recognition_get_number_of_objects(camera);
  // const WbCameraRecognitionObject *objects = wb_camera_recognition_get_objects(camera);
  get_Recognition_info();
  RecognitionInfo mp = recognitionRawvalue;
  int number_of_objects = mp.number_of_objects;
  myObject *objects = mp.objects;

  for (int i = 0; i < number_of_objects; ++i)
  {
    // if (objects[i].position[2] < -5)
    //   continue;
    if (strcmp(objects[i].model, good_name) == 0)
    {
      *item_grasped_id = objects[i].id;
      // RecognizationInfo mp = my_get_recognization_info();
      // int number_of_objects = mp.number_of_objects;
      // myObject *objects = mp.objects;
      printf("Item_Grasped_Id: %d %d\n", Item_Grasped_Id, *item_grasped_id);
      printf("num_of_obj: %d model: %s\n", number_of_objects, objects[i].model);
      printf("size: %lf %lf\n", objects[i].size[0], objects[i].size[1]);
      printf("position: %lf %lf %lf\n", objects[i].position[0], objects[i].position[1], objects[i].position[2]);
      int Shelfx = max(0, floor((objects[i].position[0] + 0.84) * 4.17 + 0.5)); //左右 平均间隔0.24（架子宽度0.25）右移后对应一个系数 四舍五入
      int Shelfy = (objects[i].position[1] < -0.2) ? 0 : 1;                     //上下层 -0.20  为上下分界
      printf("shelfx: %d y:%d\n", Shelfx, Shelfy);
      TargetIndex = Shelfx + Shelfy * 8;
      return true;
    }
  }
  return false;
}

//前部摄像头校准并抓取
bool Aim_and_Grasp(int *grasp_state, int objectID)
{
  ros::Subscriber feedback = n->subscribe(ROS_NODE_NAME+"/left_finger_motor/force_feedback", 10, left_finger_Callback);
  //饼干盒ID43 水瓶ID56
  get_Recognition_info();
  RecognitionInfo mp = recognitionRawvalue;
  int number_of_objects = mp.number_of_objects;
  auto objects = mp.objects;
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
        ROS_INFO("SIZE: %.2f", objects[i].size[0]);
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

        MoveShortDistance(grasp_target_posture, 0.05);

        double grasp_threshold = 0.02;
        if (fabs(objects[i].position[0]) < grasp_threshold && fabs(objects[i].position[2] - grasp_dis_set) < grasp_threshold)
        {
          *grasp_state += 1;
          printf("对准了！\n");
          base_reset();
          Update();
          // 用视觉先来个抓手基本值
          // printf("物体大小: %lf %lf\n", objects[i].size_on_image.x, objects[i].size_on_image.y);
          moveFingers(width = objects[i].size[0] / 2);
          robot_step(100 * TIME_STEP);
        }
      }
      else if (*grasp_state == 1) //抓
      {
        double grasp_force_threshold = 40.0;
        while(!finger_call_back){
          ros::spinOnce();
        }
        finger_call_back = false;
        if ( gripperRawvalue[1].data > -grasp_force_threshold)
          moveFingers(width -= 0.0003); //步进
        else
        {
          printf("当前电机力反馈：%.3f\n", gripperRawvalue[1].data);
          printf("抓紧了！\n");
          robot_step(100*TIME_STEP); //等他抓稳定
          while(!finger_call_back){
          ros::spinOnce();
          }
          finger_call_back = false;
          if (gripperRawvalue[1].data <= -grasp_force_threshold)
          {
            robot_step(100*TIME_STEP);
            *grasp_state += 1;
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

//商品名转换
int name2index(char *name)
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

// 仿真前进
static void step(){
  if (!timeStepClient.call(timeStepSrv) || !timeStepSrv.response.success)
  {
      ROS_ERROR("Failed to call service time_step for next step.");
  }
}

// 仿真前进
void robot_step(int time){
  timeStepSrv.request.value = time;
  if (!timeStepClient.call(timeStepSrv) || !timeStepSrv.response.success)
  {
      ROS_ERROR("Failed to call service time_step for next step.");
  }
  timeStepSrv.request.value = TIME_STEP;
}

// 更新
void Update()
{
    SetMotorSpeed();  //设置电机速度
    //SetArmPosition(); //设置机械臂各关节位姿
    /*
    if (grabStatus == REALSE)
        ClawRealse();
    else
        ClawGrab();
    */
}

//软件仿真延时
static void passive_wait(double sec)
{
  double start_time = ros::Time::now().toSec();
  do
  {
    step();
  } while (start_time + sec > ros::Time::now().toSec());
}

void Quit(int sig)
{
    ROS_INFO("User stopped the 'ros_test' node.");
    timeStepSrv.request.value = 0;
    timeStepClient.call(timeStepSrv);
    ros::shutdown();
    exit(0);
}

//设置手爪开合大小
void moveFingers(double position)
{
  set_gripper_velocity( 1, GRIPPER_MOTOR_MAX_SPEED);
  set_gripper_velocity( 2, GRIPPER_MOTOR_MAX_SPEED);
  set_gripper_position( 1, position);
  set_gripper_position( 2, position);
}

//设置机械臂上升高度
void lift(double position)
{
  set_gripper_velocity(0, GRIPPER_MOTOR_MAX_SPEED);
  set_gripper_position(0, position);
}

bool set_gripper_velocity(int index, float speed){
        ros::ServiceClient setVelocityClient;
        webots_ros::set_float setVelocitySrv;
        //请求服务 load_good_bot/xxx_motor/set_velocity
        setVelocityClient = n->serviceClient<webots_ros::set_float>(ROS_NODE_NAME + std::string("/") + std::string(gripperNames[index]) + std::string("/set_velocity"));
        setVelocitySrv.request.value = speed;
        if (setVelocityClient.call(setVelocitySrv) && setVelocitySrv.response.success == 1)
            ;//ROS_INFO("Velocity set to %0.2f for motor %s.", setVelocitySrv.request.value,motorNames[i]);
        else
        {
            ROS_ERROR("Failed to call service set_velocity on motor %s.", gripperNames[index]);
            return false;
        }
        return true;
}

bool set_gripper_position(int index, float position){
        ros::ServiceClient setPositionClient;
        webots_ros::set_float setPositionSrv;
        //请求服务 SuperMarketRobot/xxx_motor/set_position
        setPositionClient = n->serviceClient<webots_ros::set_float>(ROS_NODE_NAME + std::string("/") + std::string(gripperNames[index]) + std::string("/set_position"));
        setPositionSrv.request.value = position;
        if (setPositionClient.call(setPositionSrv) && setPositionSrv.response.success)
            ;//ROS_INFO("Position set to %0.2f for motor %s.", setPositionSrv.request.value, motorNames[i]);
        else
        {
            ROS_ERROR("Failed to call service set_position on motor %s.", gripperNames[index]);
            return false;
        }
        return true;
}

////////////////底层函数////////////////

bool SetMotorPosition()
{
    for (int i = 0; i < NMOTORS; ++i)
    {
        ros::ServiceClient setPositionClient;
        webots_ros::set_float setPositionSrv;
        //请求服务 SuperMarketRobot/wheel_xxx/set_position
        setPositionClient = n->serviceClient<webots_ros::set_float>(ROS_NODE_NAME + std::string("/") + std::string(motorNames[i]) + std::string("/set_position"));
        setPositionSrv.request.value = motorPositions[i];
        if (setPositionClient.call(setPositionSrv) && setPositionSrv.response.success)
            ;//ROS_INFO("Position set to %0.2f for motor %s.", setPositionSrv.request.value, motorNames[i]);
        else
        {
            ROS_ERROR("Failed to call service set_position on motor %s.", motorNames[i]);
            return false;
        }
    }
    return true;
}

bool SetMotorSpeed()
{
    for (int i = 0; i < NMOTORS; ++i)
    {
        ros::ServiceClient setVelocityClient;
        webots_ros::set_float setVelocitySrv;
        //请求服务 load_good_bot/wheel_xxx/set_velocity
        setVelocityClient = n->serviceClient<webots_ros::set_float>(ROS_NODE_NAME + std::string("/") + std::string(motorNames[i]) + std::string("/set_velocity"));
        setVelocitySrv.request.value = motorSpeeds[i];
        if (setVelocityClient.call(setVelocitySrv) && setVelocitySrv.response.success == 1)
            ;//ROS_INFO("Velocity set to %0.2f for motor %s.", setVelocitySrv.request.value,motorNames[i]);
        else
        {
            ROS_ERROR("Failed to call service set_velocity on motor %s.", motorNames[i]);
            return false;
        }
    }
    return true;
}

void base_reset()
{
    motorSpeeds[0] = 0.0;
    motorSpeeds[1] = 0.0;
    motorSpeeds[2] = 0.0;
    motorSpeeds[3] = 0.0;
}

void base_forwards()
{
    motorSpeeds[0] = baseSpeed;
    motorSpeeds[1] = baseSpeed;
    motorSpeeds[2] = baseSpeed;
    motorSpeeds[3] = baseSpeed;
}

void base_backwards()
{
    motorSpeeds[0] = -baseSpeed;
    motorSpeeds[1] = -baseSpeed;
    motorSpeeds[2] = -baseSpeed;
    motorSpeeds[3] = -baseSpeed;
}

void base_turn_left()
{
    motorSpeeds[0] = -baseSpeed;
    motorSpeeds[1] = baseSpeed;
    motorSpeeds[2] = -baseSpeed;
    motorSpeeds[3] = baseSpeed;
}

void base_turn_right()
{
    motorSpeeds[0] = baseSpeed;
    motorSpeeds[1] = -baseSpeed;
    motorSpeeds[2] = baseSpeed;
    motorSpeeds[3] = -baseSpeed;
}

void base_strafe_left()
{
    motorSpeeds[0] = baseSpeed;
    motorSpeeds[1] = -baseSpeed;
    motorSpeeds[2] = -baseSpeed;
    motorSpeeds[3] = baseSpeed;
}

void base_strafe_right()
{
    motorSpeeds[0] = -baseSpeed;
    motorSpeeds[1] = baseSpeed;
    motorSpeeds[2] = baseSpeed;
    motorSpeeds[3] = -baseSpeed;
}

void base_goto_init()
{
    goto_data.v_target.u = 0.0;
    goto_data.v_target.v = 0.0;
    goto_data.alpha = 0.0;
    goto_data.reached = false;
}

void base_goto_set_target(double x, double z, double alpha)
{
    goto_data.v_target.u = x;
    goto_data.v_target.v = z;
    goto_data.alpha = alpha;
    goto_data.reached = false;
}

void base_goto_run()
{
    //ROS_INFO("RUN");
    motorSpeeds[0] = 0;
    motorSpeeds[1] = 0;
    motorSpeeds[2] = 0;
    motorSpeeds[3] = 0;
    // compute 2d vectors
    Vector2 v_gps = {gpsRawValue.latitude, gpsRawValue.longitude};
    //ROS_INFO("GPS:x:%f,z:%f", gpsRawValue.point.x, gpsRawValue.point.z);
    Vector2 v_front = {compassRawValue.magnetic_field.x, compassRawValue.magnetic_field.y};
    //ROS_INFO("Compass:x:%f,z:%f",compassRawValue.magnetic_field.x, compassRawValue.magnetic_field.y);
    Vector2 v_right = {-v_front.v, v_front.u};
    Vector2 v_north = {1.0, 0.0};

    // compute distance
    Vector2 v_dir;
    vector2_minus(&v_dir, &goto_data.v_target, &v_gps);
    double distance = vector2_norm(&v_dir);

    // compute absolute angle & delta with the delta with the target angle
    double theta = vector2_angle(&v_front, &v_north) + M_PI;
    double delta_angle = theta - goto_data.alpha;
    //ROS_INFO("theta:%.2f delta_angle:%.2f", theta, delta_angle);

    // compute the direction vector relatively to the robot coordinates
    // using an a matrix of homogenous coordinates
    Matrix33 transform;
    matrix33_set_identity(&transform);
    transform.a.u = v_front.u;
    transform.a.v = v_right.u;
    transform.b.u = v_front.v;
    transform.b.v = v_right.v;
    transform.c.u = -v_front.u * v_gps.u - v_front.v * v_gps.v;
    transform.c.v = -v_right.u * v_gps.u - v_right.v * v_gps.v;
    Vector3 v_target_tmp = {goto_data.v_target.u, goto_data.v_target.v, 1.0};
    Vector3 v_target_rel;
    matrix33_mult_vector3(&v_target_rel, &transform, &v_target_tmp);

    // compute the speeds
    // -> first stimulus: delta_angle

    motorSpeeds[0] = -delta_angle / M_PI * K1;
    motorSpeeds[1] = delta_angle / M_PI * K1;
    motorSpeeds[2] = -delta_angle / M_PI * K1;
    motorSpeeds[3] = delta_angle / M_PI * K1;

    // -> second stimulus: u coord of the relative target vector
    motorSpeeds[0] += v_target_rel.u * K2;
    motorSpeeds[1] += v_target_rel.u * K2;
    motorSpeeds[2] += v_target_rel.u * K2;
    motorSpeeds[3] += v_target_rel.u * K2;

    // -> third stimulus: v coord of the relative target vector
    motorSpeeds[0] += -v_target_rel.v * K3;
    motorSpeeds[1] += v_target_rel.v * K3;
    motorSpeeds[2] += v_target_rel.v * K3;
    motorSpeeds[3] += -v_target_rel.v * K3;

    // apply the speeds
    int i;
    for (i = 0; i < 4; i++)
    {
        motorSpeeds[i] /= (K1 + K2 + K2); // number of stimuli (-1 <= motorSpeeds <= 1)
        motorSpeeds[i] *= MAX_SPEED;      // map to speed (-SPEED <= motorSpeeds <= SPEED)

        // added an arbitrary factor increasing the convergence speed
        motorSpeeds[i] *= 30.0;
        motorSpeeds[i] = bound(motorSpeeds[i], -MAX_SPEED, MAX_SPEED);
    }
    // check if the taget is reached
    //ROS_INFO("delta_angle:%0.2f,distance:%0.2f", delta_angle, distance);
    if (distance < DISTANCE_TOLERANCE && delta_angle < ANGLE_TOLERANCE && delta_angle > -ANGLE_TOLERANCE)
        goto_data.reached = true;
}

bool base_goto_reached()
{
    return goto_data.reached;
}

/////////////math///////////
void vector3_set_values(Vector3 *vect, double u, double v, double w)
{
    vect->u = u;
    vect->v = v;
    vect->w = w;
}

void matrix33_set_values(Matrix33 *m, double au, double av, double aw, double bu, double bv, double bw, double cu, double cv, double cw)
{
    vector3_set_values(&(m->a), au, av, aw);
    vector3_set_values(&(m->b), bu, bv, bw);
    vector3_set_values(&(m->c), cu, cv, cw);
}

void matrix33_set_identity(Matrix33 *m)
{
    matrix33_set_values(m, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0);
}

void matrix33_mult_vector3(Vector3 *res, const Matrix33 *m, const Vector3 *v)
{
    res->u = m->a.u * v->u + m->b.u * v->v + m->c.u * v->w;
    res->v = m->a.v * v->u + m->b.v * v->v + m->c.v * v->w;
    res->w = m->a.w * v->u + m->b.w * v->v + m->c.w * v->w;
}

double vector2_norm(const Vector2 *v)
{
    return sqrt(v->u * v->u + v->v * v->v);
}

void vector2_minus(Vector2 *v, const Vector2 *v1, const Vector2 *v2)
{
    v->u = v1->u - v2->u;
    v->v = v1->v - v2->v;
}

double vector2_angle(const Vector2 *v1, const Vector2 *v2)
{
    return atan2(v2->v, v2->u) - atan2(v1->v, v1->u);
}

double bound(double v, double a, double b)
{
    return (v > b) ? b : (v < a) ? a
                                 : v;
}
