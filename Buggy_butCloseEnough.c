#include "msp.h"
#include "lcdlib (1).h"
#define PIN_LENGTH 4
#define BUZZER_PIN BIT5  // Define the buzzer pin (P2.5)

void setup(void);
void handleKeyPress(void);
void PORT3_IRQHandler(void);
void PORT5_IRQHandler(void);
void PORT6_IRQHandler(void);  // Adding PORT6 IRQ handler
void configurePWM(void);
void countDown(void);
void precountDown(void);
void configureServoTimer(void);
void TA0_0_IRQHandler(void);

volatile int timeIndex = 0;
volatile char enteredTime[PIN_LENGTH + 1] = {0}; // Buffer to store the time
volatile char lastKeyPress = '\0';
volatile int s = 0, m = 0; // Timer variables
volatile int timerRunning = 0; // Flag to indicate if the timer is running
volatile int servoDutyCycle = 1500; // Initial duty cycle for servo (1.5ms pulse width)
volatile int buzzerDutyCycle = 0; // Initial buzzer duty cycle

void main(void) {
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD; // Stop watchdog timer
    lcdInit();
    lcdClear();
    lcdSetText("Enter Time:", 0, 0);
    setup();
    configurePWM();
    configureServoTimer();
    __enable_irq(); // Enable global interrupts

    while (1) {
        if (timerRunning) {
            countDown();
            __delay_cycles(3000000); // Delay for 1 second (assuming 3 MHz clock)
        } else {
            TIMER_A0->CCR[1] = 0; // Ensure servo is stopped when timer is not running
            __sleep();
            __no_operation();
        }
    }
}

void setup(void) {
    P2->DIR |= BUZZER_PIN;  // Set P2.5 as output for the buzzer
    P2->OUT &= ~BUZZER_PIN; // Ensure the buzzer is off initially

    P5->DIR |= (BIT4 | BIT5 | BIT6 | BIT7); // Rows as output
    P5->OUT &= ~(BIT4 | BIT5 | BIT6 | BIT7); // Set rows low
    P3->DIR &= ~(BIT5 | BIT6 | BIT7); // Columns as input
    P3->REN |= (BIT5 | BIT6 | BIT7); // Enable pull-up/pull-down resistors
    P3->OUT |= (BIT5 | BIT6 | BIT7); // Pull-up resistors

    // Enable interrupts for P5.4, P5.5, P5.6, and P5.7
    P5->IE |= (BIT4 | BIT5 | BIT6 | BIT7); // Enable interrupt on P5.4, P5.5, P5.6, and P5.7
    P5->IES |= (BIT4 | BIT5 | BIT6 | BIT7); // Set interrupt to trigger on high-to-low transition
    P5->IFG &= ~(BIT4 | BIT5 | BIT6 | BIT7); // Clear interrupt flags
    NVIC->ISER[1] = 1 << ((PORT5_IRQn) & 31); // Enable Port 5 interrupt in NVIC

    // Enable interrupts for P3.5, P3.6, and P3.7
    P3->IE |= (BIT5 | BIT6 | BIT7); // Enable interrupt on P3.5, P3.6, and P3.7
    P3->IES |= (BIT5 | BIT6 | BIT7); // Set interrupt to trigger on high-to-low transition
    P3->IFG &= ~(BIT5 | BIT6 | BIT7); // Clear interrupt flags
    NVIC->ISER[1] = 1 << ((PORT3_IRQn) & 31); // Enable Port 3 interrupt in NVIC


    // Configure PORT6 pins as inputs with pull-down resistors
    P6->DIR &= ~(BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7); // Set P6.0, P6.1, P6.4, P6.5, P6.6, and P6.7 as inputs
    P6->REN |= (BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7);  // Enable pull-down resistors
    P6->OUT &= ~(BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7);  // Pull-down configuration

    // Enable interrupts for P6.0, P6.1, P6.4, P6.5, P6.6, and P6.7
    P6->IE |= (BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7);
    P6->IES &= ~(BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7);  // Set interrupt to trigger on low-to-high transition
    P6->IFG &= ~(BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7); // Clear interrupt flags
    NVIC->ISER[1] = 1 << ((PORT6_IRQn) & 31); // Enable Port 6 interrupt in NVIC

    timerRunning = 0; // Ensure the timer is not running initially
}

void configurePWM() {
    P2->DIR |= BIT4; // Set P2.4 as output
    P2->SEL0 |= BIT4; // Select primary module function for P2.4
    P2->SEL1 &= ~BIT4; // Select primary module function for P2.4

    TIMER_A0->CCR[0] = 300000 - 1; // PWM Period (# of ticks in one period for 50Hz)
    TIMER_A0->CCTL[1] = TIMER_A_CCTLN_OUTMOD_7; // CCR1 reset/set mode
    TIMER_A0->CCR[1] = 0; // Initially set CCR1 PWM duty cycle to 0 (servo off)
    TIMER_A0->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR; // SMCLK, Up mode, clear TAR
}

void configureServoTimer() {
    TIMER_A1->CCR[0] = 30000 - 1; // Servo update period
    TIMER_A1->CCTL[0] = TIMER_A_CCTLN_CCIE; // Enable interrupt
    TIMER_A1->CTL = TIMER_A_CTL_SSEL__SMCLK | TIMER_A_CTL_MC__UP | TIMER_A_CTL_CLR; // SMCLK, Up mode, clear TAR

    NVIC->ISER[1] = 1 << ((TA1_0_IRQn) & 31); // Enable Timer_A1 interrupt in NVIC
}

void TA1_0_IRQHandler(void) {
    TIMER_A1->CCTL[0] &= ~TIMER_A_CCTLN_CCIFG; // Clear interrupt flag
    if (timerRunning) {
        TIMER_A0->CCR[1] = servoDutyCycle; // Update PWM duty cycle when timer is running
    } else {
        TIMER_A0->CCR[1] = 0; // Stop PWM signal to servo when timer is not running
    }
}

void PORT6_IRQHandler(void) {
    // Initial check if any flag is raised
    if ((P6->IFG & (BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7)) == 0) {
        return; // Exit if no valid button was pressed
    }
    __delay_cycles(30000);  // Debounce delay

    // Determine which button was pressed and set the time accordingly
    if ((P6->IFG & BIT0) && (P6->IN & BIT0)) {
        m = 0;
        s = 30;  // Quick Reheat
    } else if ((P6->IFG & BIT1) && (P6->IN & BIT1)) {
        m = 1;
        s = 0;   //Soup
    } else if ((P6->IFG & BIT4) && (P6->IN & BIT4)) {
        m = 1;
        s = 30;   ///Noodles
    } else if ((P6->IFG & BIT5) && (P6->IN & BIT5)) {
        m = 2;
        s = 0;   //Pizza Pocket
    } else if ((P6->IFG & BIT6) && (P6->IN & BIT6)) {
        m = 2;
        s = 30;   //Popcorn
    } else if ((P6->IFG & BIT7) && (P6->IN & BIT7)) {
        m = 3;
        s = 0; //Frozen Burrito
    } else {
        P6->IFG &= ~(BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7); // Clear interrupt flags
        return; // Exit if no valid button was pressed
    }

    // Sound the buzzer for 1 second
    P2->OUT |= BUZZER_PIN;  // Turn on the buzzer
    __delay_cycles(300000);  // Delay for 0.1 second (assuming 3MHz clock)
    P2->OUT &= ~BUZZER_PIN;  // Turn off the buzzer
    timerRunning = 1;  // Start the timer
    lcdClear();
    lcdSetText("Time left!", 0, 0);  // Display time set message
    P6->IFG &= ~(BIT0 | BIT1 | BIT4 | BIT5 | BIT6 | BIT7); // Clear interrupt flags
}

char keypad(void) {
    const char keys[4][3] = { {'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'*', '0', '#'} };
    int row, col;
    for (row = 0; row < 4; row++) {
        P5->OUT |= (BIT4 | BIT5 | BIT6 | BIT7); // Set all rows high
        P5->OUT &= ~(BIT4 << row); // Set the current row low
        for (col = 0; col < 3; col++) {
            if (!(P3->IN & (BIT5 << col))) {
                __delay_cycles(30000); // Debouncing delay
                if (!(P3->IN & (BIT5 << col))) {
                    return keys[row][col];
                }
            }
        }
    }
    return '\0';
}

void PORT3_IRQHandler(void) {
    handleKeyPress();
    P3->IFG &= ~(BIT5 | BIT6 | BIT7); // Clear interrupt flags
}

void PORT5_IRQHandler(void) {
    handleKeyPress();
    P5->IFG &= ~(BIT4 | BIT5 | BIT6 | BIT7); // Clear interrupt flags
}

void handleKeyPress(void) {
    int i;
    if (!timerRunning) {  // Only accept input if the timer is not running
        char keyPress = keypad();
        if (keyPress != '\0' && keyPress != lastKeyPress) {  // Check for a valid and new key press
            lastKeyPress = keyPress;
            if (keyPress >= '0' && keyPress <= '9' && timeIndex < PIN_LENGTH) {  // Check for digit and buffer overflow
                enteredTime[timeIndex++] = keyPress;  // Store the digit
                lcdSetText(enteredTime, 11, 0);  // Display entered digits

                // Sound the buzzer for key press feedback
                P2->OUT |= BUZZER_PIN;  // Turn on the buzzer
                __delay_cycles(300000);  // Delay for 0.1 second (assuming 3MHz clock)
                P2->OUT &= ~BUZZER_PIN;  // Turn off the buzzer
            }

            if (timeIndex == PIN_LENGTH) {  // When 4 digits are entered
                enteredTime[PIN_LENGTH] = '\0';  // Null-terminate the string
                int totalTime = atoi(enteredTime);  // Convert entered time to an integer

                // Calculate minutes and seconds
                m = totalTime / 100;  // Extract minutes
                s = totalTime % 100;  // Extract seconds

                // Normalize the time if seconds >= 60
                if (s >= 60) {
                    m += s / 60;
                    s %= 60;
                }

                timerRunning = 1;  // Start the timer
                timeIndex = 0;  // Reset for next time entry

                // Clear the buffer for next input
                for (i = 0; i < PIN_LENGTH; i++) {
                    enteredTime[i] = '\0';
                }

                lcdClear();
                lcdSetText("Time left!", 0, 0);  // Confirm time set
                __delay_cycles(600000);  // Prevent immediate keypress after
            }
        }
        __delay_cycles(600000);  // Debounce delay
        lastKeyPress = '\0';  // Reset last key press
    }
}




void countDown() {
    int i;
    if (timerRunning) {
        if (s == 0 && m == 0) {
            timerRunning = 0; // Stop the timer when it reaches 00:00
            lcdClear();
            lcdSetText("Time's up!", 0, 0);

            TIMER_A0->CCR[1] = 0; // Stop PWM signal to servo

            // Make 4 short beeps
            for (i = 0; i < 4; i++) {
                P2->OUT |= BUZZER_PIN;  // Turn on the buzzer
                __delay_cycles(1500000);  // Delay for 0.1 second (assuming 3MHz clock)
                P2->OUT &= ~BUZZER_PIN;  // Turn off the buzzer
                __delay_cycles(1500000);  // Delay for 0.1 second (assuming 3MHz clock)
            }

            delay_ms(5000); // Show "Time's Up!" message for 5 seconds
            lcdClear();
            lcdSetText("Enter Time:", 0, 0); // Revert to "Enter Time!"

            // Reset variables
            timeIndex = 0;
            lastKeyPress = '\0';
            for (i = 0; i < PIN_LENGTH; i++) {
                enteredTime[i] = '\0'; // Clear the enteredTime buffer
            }
        } else {
            if (s == 0) {
                s = 59;
                m--;
            } else {
                s--;
            }
            char timeStr[6];
            sprintf(timeStr, "%02d:%02d", m, s);
            lcdSetText(timeStr, 11, 0); // Update time next to "Enter Time:"

            // Adjust duty cycle to spin the servo motor
            TIMER_A0->CCR[1] = servoDutyCycle; // 1.5ms pulse width for 0 degrees
            __delay_cycles(300000); // Increased delay to slow down the servo motor
        }
    }
}
