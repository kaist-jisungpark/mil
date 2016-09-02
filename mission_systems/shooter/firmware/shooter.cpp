#include <ros.h>
#include <std_msgs/Int8.h>
#include <std_msgs/String.h>
#include <std_srvs/Trigger.h>
#include <navigator_msgs/ShooterManual.h>

#include <Arduino.h>

#include <Servo.h>

#define USE_LINEAR_FEEDER

const int SHOOTER_PIN = 3;
const int FEEDER_A_PIN = 8;
const int FEEDER_B_PIN = 9;
const int FEEDER_PWM_PIN = 5;
ros::NodeHandle nh;
class SpeedController
{
  protected:
    bool reversed;
    virtual void _set(int s);
    virtual int _get();
    SpeedController()
    {
      reversed = false;
    }
  public:
    void set(int s)
    {
      if (reversed) _set(-s);
      else _set(s);
    }
    int get()
    {
      if (reversed) return -_get();
      else return _get();
    }
    void setReversed(bool r)
    {
      reversed = r;
    }
    void on()
    {
      set(100);
    }
    void off()
    {
      set(0);
    }
    void reverse()
    {
      set(-100);
    }
};

//Class for the Victor 883 speed controller used to control the shooter motors
class Victor : public SpeedController
{
  private:
    Servo controller;
    int goal;
    int cur;
    int pin;
    //Internal set command to write to controller PWM
    void _set(int s)
    {
      goal = map(s,-100,100, 1000,2000);
      nh.logdebug("Victor Goal = " + goal);
    }
    int _get()
    {
      return map(cur,1000,2000,-100,100);
    }
  public:
    Victor(int p)
    {
      pin = p;
      controller = Servo();
      goal = 1500;
      cur = goal;
    }
    void init()
    {
      controller.attach(pin);
      goal = 1500;
      cur = goal;
      controller.writeMicroseconds(cur);
    }
    //Should be called in each loop so PWM slowly ramps up, doesn't work otherwise
    void run()
    {
      if (cur != goal)
      {
        if (goal == 1500) cur = 1500;
        else if (goal < 1500)
        {
          cur -= 100;
        }
        else if (goal > 1500)
        {
          cur += 100;
        }
        controller.writeMicroseconds(cur);
      }
    }
};
Victor shooter(SHOOTER_PIN);

//Class for controlling the Pololu speed controller used for the feeder
class Pololu : public SpeedController
{
  private:
    int inA_pin;
    int inB_pin;
    int pwm_pin;
    int speed;
    void _set(int s)
    {
      speed = s;
      if (s == 0) {
        digitalWrite(inA_pin,LOW);
        digitalWrite(inB_pin,LOW);
        analogWrite(pwm_pin,0);
      } else if(s < 0) {
        digitalWrite(inA_pin,HIGH);
        digitalWrite(inB_pin,LOW);
        analogWrite(pwm_pin,map(s,0,-100,0,255));
      } else if (s > 0) {
        digitalWrite(inA_pin,LOW);
        digitalWrite(inB_pin,HIGH);
        analogWrite(pwm_pin,map(s,0,100,0,255));
      }     
    }
    int _get()
    {
      return speed;
    }
  public:
    Pololu(int a, int b, int pwm)
    {
      inA_pin = a;
      inB_pin = b;
      pwm_pin = pwm;
      speed = 0;
    }
    void init()
    {
      pinMode(inA_pin,OUTPUT);
      pinMode(inB_pin,OUTPUT);
      pinMode(pwm_pin,OUTPUT);
    }
};
Pololu feeder(FEEDER_A_PIN,FEEDER_B_PIN,FEEDER_PWM_PIN);

#ifdef USE_LINEAR_FEEDER
class AutoController
{
  private:
    //All times in milliseconds
    static const unsigned long SPIN_UP_TIME = 1000; //Time to spin up flywheels before feeding balls in
    static const unsigned long RETRACT_TIME = 1000; //Time to retract actuator to allow ball to fall into feeding tube
    static const unsigned long LOAD_TIME = 650; //Time to extend actuator to preload ball for quick firing
    static const unsigned long QUICKFIRE_TIME = 300; //Time to extend actuator with preloaded ball for quick firing
    /* Represents what the controller is currently doing
     * 0 = finished fireing/loading or stopped
     * 1 = preloading for quick fireing
     * 2 = fireing from preloaded state
     */
    int state;
    unsigned long start_load_time;
    unsigned long start_fire_time;
    bool loaded;
    void reset()
    {
      state = 0;
      start_load_time = 0;
      start_fire_time = 0;
      loaded = false;
    }
    void runLoad()
    {
      unsigned long cur_time = millis() - start_load_time;
      if (cur_time < RETRACT_TIME)
      {
        feeder.reverse();
      } else if (cur_time < (RETRACT_TIME + LOAD_TIME) )
      {
        feeder.on();
      } else {
				feeder.off();
        shooter.on();
        state = 0;
        loaded = true;
        start_load_time = 0;
      }
    }
    void runFire()
    {
      unsigned long cur_time = millis() - start_fire_time;
      if (cur_time < QUICKFIRE_TIME)
      {
        shooter.on();
        feeder.on();
      } else {
        start_fire_time = 0;
        feeder.off();
        shooter.off();
        loaded = false;
        state = 0;
      }
    }
  public:
    AutoController()
    {
      reset();
    }
    void load()
    {
      start_load_time = millis();
      loaded = false;
      state = 1;
    }
    void fire()
    {
      start_fire_time = millis();
      state = 2;
    }
    void cancel()
    {
      feeder.off();
			shooter.off();
      reset();
    }
    void run()
    {
      switch (state) 
      {
        case 0:
          break;
        case 1:
          runLoad();
          break;
        case 2:
          runFire();
          break;
      }
    }

};
#else
class AutoController
{
  private:
    static const unsigned long SPIN_UP_TIME = 1000; //Constant for time to spin up flywheels before feeding balls in
    static const unsigned long SHOOT_TIME = 12000; //Time to shoot all 4 balls once after they start being fed in
    static const unsigned long TOTAL_TIME = SPIN_UP_TIME + SHOOT_TIME;
    static const int FEED_SPEED = 50; //speed (out of 100) to set feeder motor to when feeding balls

    unsigned long start_shoot_time;
    bool auto_shoot;
  public:
		AutoController()
		{
			start_shoot_time = 0;
			auto_shoot = false;
		}
    void shoot()
    {
      feeder.off();
			shooter.off();
			auto_shoot = true;
			start_shoot_time = millis();
    }
		void cancel()
    {
			feeder.off();
			shooter.off();
			auto_shoot = false;
		}
    bool shooting()
    {
      return auto_shoot;
    }
		void run()
		{
			if (auto_shoot)
			{
				unsigned long time_since_start = millis() - start_shoot_time;
				if (time_since_start < SPIN_UP_TIME) shooter.on();
				else if (time_since_start > SPIN_UP_TIME && time_since_start < TOTAL_TIME) feeder.on(); //feeder.motor.set(FEED_SPEED);
				else if (time_since_start > TOTAL_TIME) cancel();
			}
		}	
};
#endif
AutoController autoController;
class Comms
{
  private:
    //ROS
    
    #ifdef USE_LINEAR_FEEDER
    ros::ServiceServer<std_srvs::Trigger::Request,std_srvs::Trigger::Response> fireService;
    ros::ServiceServer<std_srvs::Trigger::Request,std_srvs::Trigger::Response> loadService;
    ros::ServiceServer<std_srvs::Trigger::Request,std_srvs::Trigger::Response> cancelService;
    ros::ServiceServer<navigator_msgs::ShooterManual::Request,navigator_msgs::ShooterManual::Response> manualService;
    #else
    ros::ServiceServer<std_srvs::Trigger::Request,std_srvs::Trigger::Response> fireService;
    ros::ServiceServer<std_srvs::Trigger::Request,std_srvs::Trigger::Response> cancelService;
    ros::ServiceServer<navigator_msgs::ShooterManual::Request,navigator_msgs::ShooterManual::Response> manualService;    
    #endif

    #ifdef USE_LINEAR_FEEDER
    static void fireCallback(const std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
      autoController.fire();
      res.success = true;
    }
    static void loadCallback(const std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
      autoController.load();
      res.success = true;
    }
    static void cancelCallback(const std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
      autoController.cancel();
      res.success = true;
    }
    static void manualCallback(const navigator_msgs::ShooterManual::Request &req, navigator_msgs::ShooterManual::Response &res)
    {
      autoController.cancel();
      feeder.set(req.feeder);
      shooter.set(req.shooter);
    }
    #else
    static void fireCallback(const std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
      autoController.shoot();
      res.success = true;
    }
    static void cancelCallback(const std_srvs::Trigger::Request &req, std_srvs::Trigger::Response &res)
    {
      autoController.cancel();
      res.success = true;
    }
    static void manualCallback(const navigator_msgs::ShooterManual::Request &req, navigator_msgs::ShooterManual::Response &res)
    {
      nh.logdebug("Manual: Feeder="+req.feeder + " Shooter="+req.shooter);
      autoController.cancel();
      feeder.set(req.feeder);
      shooter.set(req.shooter);
      res.success = true;
    }    
    #endif

  public:
    Comms() :
      #ifdef USE_LINEAR_FEEDER
      fireService("/shooter/fire", &fireCallback),
      loadService("/shooter/load", &loadCallback),
      cancelService("/shooter/cancel", &cancelCallback),
      manualService("/shooter/manual",&manualCallback)     
      #else
      fireService("/shooter/fire", &fireCallback),
      cancelService("/shooter/cancel", &cancelCallback),
      manualService("/shooter/manual",&manualCallback)
      #endif
    {
      pinMode(13,OUTPUT);
    }
    void init()
    {
      nh.initNode();

      #ifdef USE_LINEAR_FEEDER
      nh.advertiseService(fireService);
      nh.advertiseService(loadService);
      nh.advertiseService(cancelService);
      nh.advertiseService(manualService);     
      #else
      nh.advertiseService(fireService);
      nh.advertiseService(cancelService);
      nh.advertiseService(manualService);
      #endif
    }
    void run()
    {
      nh.spinOnce();
    }

};

Comms com;
void setup()
{
  shooter.setReversed(true);
  shooter.init();
  feeder.init();
  com.init();
}

void loop()
{
  com.run();
  autoController.run();
  shooter.run();
  delay(100);
}
