/**************************************************************
 * Class: CSC-615-01 Spring 2024
 * Name: Collins Gichohi, Yakoub Alkabsh, Mohammad Dahbour, Diego 
 * Flores
 * Student ID: 922440815
 * Github-Name: gsnilloC
 * Project: Assignment 3 - Start Your Motors
 *
 * File: termProject.c
 *
 * Description: This file is the main controller for the PI.
 * It utilizes threads to read from many pins, controlling
 * the turning of the car based of the values read from the
 * Sensors. It also manages going around an obstacle
 **************************************************************/
#include "termProject.h"

volatile sig_atomic_t cleaned_up = 0;
pthread_t leftLineThread, rightLineThread, frontObstacleThread, sideObstacleThread;
volatile int buttonPressed = 0;

void buttonPressHandler(int gpio, int level, uint32_t tick)
{
    if (level == 0) // Assuming button press pulls the pin low
    {
        buttonPressed = 1;
        printf("BUTTON PRESSED!\n");
    }
}

int main(void)
{
    // Initialize system modules
    if (DEV_ModuleInit())
    {
        exit(0); // Exit if initialization fails
    }

    // Initialize GPIO library, exit with error if it fails
    if (gpioInitialise() < 0)
    {
        fprintf(stderr, "pigpio initialization failed!\n");
        return 1;
    }

    // Register signal handlers for clean exit
    signal(SIGINT, handler);
    signal(SIGTSTP, handler);

    // Configure the button pin
    gpioSetMode(BUTTON_PIN, PI_INPUT);
    gpioSetPullUpDown(BUTTON_PIN, PI_PUD_UP); // Enable pull-up resistor

    int initialButtonState = gpioRead(BUTTON_PIN);
    printf("Initial button state: %d\n", initialButtonState);
    printf("Waiting for button press...\n");

    while (!buttonPressed)
    {
        int currentButtonState = gpioRead(BUTTON_PIN); // Read the button state
        if (currentButtonState == PI_LOW && initialButtonState == PI_HIGH)
        {
            // Button was just pressed
            buttonPressHandler(BUTTON_PIN, currentButtonState, 0); // Call the handler
        }

        initialButtonState = currentButtonState; // Update the previous state for the next loop iteration
        gpioDelay(10000);                        // 10 ms delay
    }

    printf("Button press detected. Starting motors...\n");

    // Configure GPIO pins
    gpioSetMode(LEFT_LINE_PIN, PI_INPUT);
    gpioSetMode(RIGHT_LINE_PIN, PI_INPUT);

    gpioSetMode(FRONT_OBSTACLE_ECHO, PI_INPUT);
    gpioSetMode(FRONT_OBSTACLE_TRIG, PI_OUTPUT);

    gpioSetMode(SIDE_OBSTACLE_ECHO, PI_INPUT);
    gpioSetMode(SIDE_OBSTACLE_TRIG, PI_OUTPUT);

    // Initialize sensors and motor driver
    initStructs();
    motorInit();

    // Create and start threads for sensor routines
    pthread_create(&leftLineThread, NULL, routine, (void *)&leftLine);
    pthread_create(&rightLineThread, NULL, routine, (void *)&rightLine);
    pthread_create(&frontObstacleThread, NULL, measureDistance, (void *)&frontObstacle);
    pthread_create(&sideObstacleThread, NULL, measureDistance, (void *)&sideObstacle);

    // Control loop to manage motor based on sensor inputs
    while (!cleaned_up)
    {
        printf("front obstacle distance: %d\n", frontObstacle.distance);
        if (frontObstacle.distance < 17 && frontObstacle.distance > 5)
        {
            avoidObstacle();
        }
        else if (leftLine.val == 0 && rightLine.val == 0)
        {
            motorOn(FORWARD, MOTORA, SPEED);
            motorOn(FORWARD, MOTORB, SPEED);
        }
        else if (leftLine.val != 0)
        {
            turnCar(MOTORB, &leftLine, 1);
        }
        else if (rightLine.val != 0)
        {
            turnCar(MOTORA, &rightLine, 1);
        }
    }

    // Wait for threads to finish
    pthread_join(leftLineThread, NULL);
    pthread_join(rightLineThread, NULL);
    pthread_join(frontObstacleThread, NULL);
    pthread_join(sideObstacleThread, NULL);

    return 0;
}

void avoidTurn(int tick)
{
    int pin = WHEEL_SENSOR;
    gpioSetMode(WHEEL_SENSOR, PI_INPUT);
    int val = 0;
    motorOn(BACKWARD, MOTORB, 30);
    motorOn(FORWARD, MOTORA, 60);
    int flick = gpioRead(pin); // Initial read of the pin
    int prevFlick = flick;     // Store the initial state of flick

    while (val < tick)
    {
        flick = gpioRead(pin); // Read the current state of the pin
        if (flick != prevFlick)
        {                      // Check if the state has changed
            val++;             // Increment val
            prevFlick = flick; // Update the previous state
            printf("val: %d\n", val);
        }
    }

    // gpioDelay(1200);
    printf("Turn Finished\n");
    motorStop(MOTORA);
    motorStop(MOTORB);
}

void handler(int signal)
{
    printf("Motor Stop\r\n");

    // Stop both motors
    motorStop(MOTORA);
    motorStop(MOTORB);

    printf("Interrupt... %d\n", signal);

    // Perform cleanup
    cleanup();

    // Exit program
    exit(0);
}

void cleanup()
{
    cleaned_up = 1;
    buttonPressed = 1;

    pthread_cancel(leftLineThread);
    pthread_cancel(rightLineThread);
    pthread_cancel(frontObstacleThread);
    pthread_cancel(sideObstacleThread);

    pthread_join(leftLineThread, NULL);
    pthread_join(rightLineThread, NULL);
    pthread_join(frontObstacleThread, NULL);
    pthread_join(sideObstacleThread, NULL);

    // Clean up resources on program exit
    DEV_ModuleExit();
    gpioTerminate();
}

void turnCar(UBYTE motor, Sensor *sensor, int triggered)
{
    // printf("LINE SENSOR, TURNING\n");
    UBYTE stopMotor = (motor == MOTORA) ? MOTORB : MOTORA;
    motorOn(BACKWARD, stopMotor, 35);

    while (sensor->val == triggered)
    {
        motorOn(FORWARD, motor, 65);
        usleep(1000);
    }
}

void turnCarDistance(UBYTE motor, Sensor *sensor, int target)
{

    UBYTE stopMotor = (motor == MOTORA) ? MOTORB : MOTORA;
    motorOn(BACKWARD, stopMotor, 30);

    while (sensor->distance > target)
    {
        motorOn(FORWARD, motor, 70);
        usleep(1000);
    }
}

void avoidObstacle()
{
    motorStop(MOTORA);
    motorStop(MOTORB);
    sleep(1);

    // turn to its side
    printf("side obstacle: %d\n", sideObstacle.distance);
    while (sideObstacle.distance > 13 && !cleaned_up)
    {
        turnCarDistance(MOTORB, &sideObstacle, 30);
    }

    while (sideObstacle.distance < 50)
    {
        motorOn(FORWARD, MOTORA, 60);
        motorOn(FORWARD, MOTORB, 60);
    }

    // first corner
    avoidTurn(25);
    usleep(500000);
    motorOn(FORWARD, MOTORA, 50);
    motorOn(FORWARD, MOTORB, 50);
    usleep(3000000);
    // going straight,
    printf("Side distance after first turn: %d\n", sideObstacle.distance);
    while (sideObstacle.distance > 100 && !cleaned_up)
    {
        // motorOn(FORWARD, MOTORA, 50);
        // motorOn(FORWARD, MOTORB, 50);
        printf("SIDE DISTANCE1: %d\n", sideObstacle.distance);
    }
    printf("Side distance after obstical first turn: %d\n", sideObstacle.distance);
    while (sideObstacle.distance < 44)
    {
        motorOn(FORWARD, MOTORA, 55);
        motorOn(FORWARD, MOTORB, 55);
        printf("SIDE DISTANCE2: %d\n", sideObstacle.distance);
        // motorOn(FORWARD, MOTORA, 50);
        // motorOn(FORWARD, MOTORB, 50);
        // if(sideObstacle.distance < 10){
        //     printf("ADJUSTING\n");
        //     motorOn(BACKWARD, MOTORA, 30);
        //     motorOn(FORWARD, MOTORB, 60);
        //     usleep(4000000);
        // }
        // if(sideObstacle.distance > 20){
        //     printf("ADJUSTING\n");
        //      motorOn(BACKWARD, MOTORB, 30);
        //     motorOn(FORWARD, MOTORA, 60);
        //     usleep(4000000);
        // }
    }
    // sleep(1);
    //  second corner
    avoidTurn(23);
    // usleep(750000);
    while (leftLine.val == 0)
    {
        motorOn(FORWARD, MOTORA, 60);
        motorOn(FORWARD, MOTORB, 60);
    }
    while (leftLine.val == 1)
    {
        turnCar(MOTORB, &leftLine, 1);
    }

    // sleep(3);
}

void initStructs()
{
    // Initialize line sensors
    leftLine.pin = LEFT_LINE_PIN;
    leftLine.val = 0;
    rightLine.pin = RIGHT_LINE_PIN;
    rightLine.val = 0;

    // Initialize front obstacle sensor
    frontObstacle.trigPin = FRONT_OBSTACLE_TRIG;
    frontObstacle.echoPin = FRONT_OBSTACLE_ECHO;
    frontObstacle.val = -1;

    // Initialize side obstacle sensor
    sideObstacle.trigPin = SIDE_OBSTACLE_TRIG;
    sideObstacle.echoPin = SIDE_OBSTACLE_ECHO;
    sideObstacle.val = -1;
}

void *routine(void *arg)
{
    Sensor *sensor = (Sensor *)arg;

    while (!cleaned_up)
    {
        sensor->val = gpioRead(sensor->pin);
        usleep(1000);
    }

    return NULL;
}

#include <unistd.h> // For usleep function

void *measureDistance(void *arg)
{
    Sensor *obstacleSensor = (Sensor *)arg;
    int startTick, endTick, diffTick;

    while (!cleaned_up)
    {
        // Ensure the trigger pin is low for a better first high pulse
        gpioWrite(obstacleSensor->trigPin, 0);
        gpioDelay(2); // Added delay to ensure the line is low for at least 2 microseconds

        // Send ultrasonic pulse using gpioTrigger
        gpioTrigger(obstacleSensor->trigPin, 10, 1); // Changed to use gpioTrigger for sending pulse

        // Wait for the start of the echo signal
        while (gpioRead(obstacleSensor->echoPin) == 0 && !cleaned_up)
            ;

        startTick = gpioTick();

        // Wait for the end of the echo signal
        while (gpioRead(obstacleSensor->echoPin) == 1 && !cleaned_up)
            ;

        endTick = gpioTick();

        // Calculate distance in centimeters
        diffTick = endTick - startTick;
        obstacleSensor->distance = diffTick / 58;

        usleep(60000); // Adjust delay for appropriate polling frequency
    }
    return NULL;
}