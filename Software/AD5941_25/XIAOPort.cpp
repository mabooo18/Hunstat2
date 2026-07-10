// Porting file that maps AD5940/AD5941 SPI and hardware pins to the Seeed Studio XIAO RP2040 board.

#include "ad5940.h"
#include <SPI.h>

// Microcontroller pin assignments
#define SPI_CS_AD5940_Pin D7  // Pin D7: Chip Select (active LOW) for SPI
#define AD5940_ResetPin   A1  // Pin A1: Reset line (active LOW) for AD5941
#define AD5940_IntPin     A2  // Pin A2: Interrupt Input from AD5941's Gp0 pin

// Expose internal pins globally for use in other modules
int csPin = SPI_CS_AD5940_Pin;
int resetPin = AD5940_ResetPin;
int intPin = AD5940_IntPin;

// Interrupt flag used to signal the microcontroller that the AD5941 has triggered an interrupt (e.g. data ready)
volatile static uint32_t ucInterrupted = 0;       

/**
 * @brief Retrieves the microcontroller's interrupt flag state.
 * @return 1 if an interrupt occurred, 0 otherwise.
 */
uint32_t AD5940_GetMCUIntFlag(void)
{
    return ucInterrupted;
}

/**
 * @brief Clears the microcontroller's interrupt flag.
 * @return Always returns 1.
 */
uint32_t AD5940_ClrMCUIntFlag(void)
{
    ucInterrupted = 0;
    return 1;
}

/**
 * @brief Interrupt Service Routine (ISR) triggered by the AD5941 interrupt pin falling edge.
 *        Lighter ISR that sets a flag to be processed within the main polling loop.
 */
void Ext_Int0_Handler()
{
    ucInterrupted = 1;
}

/**
 * @brief Platform-specific delay wrapper (10-microsecond multiplier).
 * @param time Count of 10-microsecond units to delay.
 */
void AD5940_Delay10us(uint32_t time)
{
    delayMicroseconds(time * 10); 
}

/**
 * @brief High-speed full-duplex SPI transfer function.
 *        Reads and writes N bytes simultaneously over SPI at 12 MHz.
 * @param pSendBuffer Pointer to the buffer containing data bytes to send.
 * @param pRecvBuff Pointer to the buffer where received data bytes will be written.
 * @param length The total number of bytes to transfer.
 */
void AD5940_ReadWriteNBytes(unsigned char *pSendBuffer, unsigned char *pRecvBuff, unsigned long length)
{
    // Begin SPI transaction with 12MHz speed, MSB-first transmission, and SPI Mode 0
    SPI.beginTransaction(SPISettings(12000000, MSBFIRST, SPI_MODE0));

    for (int i = 0; i < length; i++)
    {
        // Transfer byte-by-byte through SPI register shifter
        *pRecvBuff++ = SPI.transfer(*pSendBuffer++);  
    }

    SPI.endTransaction();  
}

/**
 * @brief Clears (pulls LOW) the SPI Chip Select pin to activate AD5941 communication.
 */
void AD5940_CsClr(void)
{
    digitalWrite(SPI_CS_AD5940_Pin, LOW);
}

/**
 * @brief Sets (pulls HIGH) the SPI Chip Select pin to deactivate AD5941 communication.
 */
void AD5940_CsSet(void)
{
    digitalWrite(SPI_CS_AD5940_Pin, HIGH);
}

/**
 * @brief Deasserts the AD5941 Reset line (pulls HIGH) to allow normal operation.
 */
void AD5940_RstSet(void)
{
    digitalWrite(AD5940_ResetPin, HIGH);
}

/**
 * @brief Asserts the AD5941 Reset line (pulls LOW) to force a hard hardware reset.
 */
void AD5940_RstClr(void)
{
    digitalWrite(AD5940_ResetPin, LOW);
}

/**
 * @brief Initializes SPI bus and registers the external falling-edge pin interrupt.
 * @param pCfg Unused config parameter block.
 * @return Always returns 0 on successful setup.
 */
uint32_t AD5940_MCUResourceInit(void *pCfg)
{
    // Start SPI bus driver library (configures SCK, MOSI, and MISO lines)
    SPI.begin();
    
    // Set pin drive modes
    pinMode(SPI_CS_AD5940_Pin, OUTPUT);
    pinMode(AD5940_ResetPin, OUTPUT);
    
    // Configure interrupt line with pullup and attach falling-edge ISR
    pinMode(AD5940_IntPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(AD5940_IntPin), Ext_Int0_Handler, FALLING);

    // Establish default idle pin levels
    AD5940_CsSet();  // CS starts HIGH (inactive)
    AD5940_RstSet(); // RESET starts HIGH (running)
    return 0;
}
