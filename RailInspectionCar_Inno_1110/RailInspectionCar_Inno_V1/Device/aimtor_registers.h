#ifndef AIMTOR_REGISTERS_H
#define AIMTOR_REGISTERS_H

#include <stdint.h>

/*
 * AIMtor伺服驱动器寄存器映射定义。
 *
 * 注意：
 * 1. 寄存器地址来源于AIMtor低压产品说明书和项目控制说明。
 * 2. 映射有效只表示软件中的地址表已经完成资料核对。
 * 3. 映射有效不代表寄存器已经通过实际伺服设备回读验证。
 * 4. 具体寄存器是否允许写入，还必须受运行时配置门禁保护。
 */

/*
 * 寄存器映射表有效标志。
 *
 * 1表示软件已经建立并核对了寄存器地址映射；
 * 0表示寄存器映射尚未确认，禁止依赖该表执行控制操作。
 */
#define AIMTOR_REGISTER_MAP_VALID               (1U)

/* 伺服控制模式寄存器，用于选择速度模式或位置模式。 */
#define AIMTOR_REG_CONTROL_MODE                 (0x0200U)

/* 失能或停止时的停机方式配置寄存器。 */
#define AIMTOR_REG_DISABLE_STOP_MODE            (0x0205U)

/* 数字输入DI1功能配置寄存器。 */
#define AIMTOR_REG_DI1_FUNCTION                 (0x0302U)

/* 数字输入DI2功能配置寄存器。 */
#define AIMTOR_REG_DI2_FUNCTION                 (0x0304U)

/* 回零或设置当前位置零点的方法配置寄存器。 */
#define AIMTOR_REG_HOME_METHOD                  (0x051EU)

/* 速度给定来源选择寄存器。 */
#define AIMTOR_REG_SPEED_SELECTION              (0x0602U)

/* 多字数据的字序配置寄存器，用于确认32位数据的高低字排列方式。 */
#define AIMTOR_REG_WORD_ORDER                   (0x0C1AU)

/* H01_00固件版本；H02_06 No.2故障停机；H0C_30超时单位ms。 */
#define AIMTOR_REG_FIRMWARE_VERSION             (0x0100U)
#define AIMTOR_REG_NO2_STOP_MODE                (0x0206U)
#define AIMTOR_REG_COMM_TIMEOUT                 (0x0C1EU)

/*
 * 回零方式值：记录当前位置。
 *
 * 该值通常用于将当前实际位置记录为新的零点，
 * 不应与会触发机械回零运动的方式混用。
 */
#define AIMTOR_HOME_RECORD_CURRENT              (6U)

/* 伺服使能控制寄存器。 */
#define AIMTOR_REG_SERVO_ENABLE                 (0x0303U)

/* 多段位置运行触发寄存器。 */
#define AIMTOR_REG_SEGMENT_TRIGGER              (0x0305U)

/* 位置指令来源选择寄存器。 */
#define AIMTOR_REG_POSITION_SOURCE              (0x0500U)

/* 电子齿轮比分子寄存器。 */
#define AIMTOR_REG_ELECTRONIC_GEAR_NUMERATOR    (0x0507U)

/* 电子齿轮分母寄存器。 */
#define AIMTOR_REG_ELECTRONIC_GEAR_DENOMINATOR  (0x0509U)

/* 脉冲输入形式配置寄存器，例如脉冲加方向等。 */
#define AIMTOR_REG_PULSE_FORMAT                 (0x050FU)

/* 速度指令来源选择寄存器。 */
#define AIMTOR_REG_SPEED_SOURCE                 (0x0600U)

/* 内部速度指令寄存器。 */
#define AIMTOR_REG_SPEED_COMMAND                (0x0603U)

/* 内部速度模式加速时间配置寄存器。 */
#define AIMTOR_REG_ACCELERATION_TIME             (0x0605U)

/* 内部速度模式减速时间配置寄存器。 */
#define AIMTOR_REG_DECELERATION_TIME             (0x0606U)

/* 实际电机速度反馈寄存器。 */
#define AIMTOR_REG_ACTUAL_SPEED                 (0x0B00U)
/* 内部实时转矩，单位为0.1%。 */
#define AIMTOR_REG_REALTIME_TORQUE              (0x0B02U)

/* 驱动器数字输出状态寄存器。 */
#define AIMTOR_REG_DO_STATE                     (0x0B05U)

/* 电机绝对位置反馈寄存器。 */
#define AIMTOR_REG_ABSOLUTE_POSITION            (0x0B07U)

/* 电机负载率反馈寄存器。 */
#define AIMTOR_REG_LOAD_RATIO                   (0x0B0CU)

/* 位置偏差反馈寄存器。 */
#define AIMTOR_REG_POSITION_DEVIATION           (0x0B0FU)

/* 伺服母线电压反馈寄存器。 */
#define AIMTOR_REG_BUS_VOLTAGE                  (0x0B1AU)
/* 相电流有效值，参数编号24为十进制，单位为0.01A。 */
#define AIMTOR_REG_PHASE_CURRENT_RMS            (0x0B18U)

/* 驱动器或电机温度反馈寄存器。 */
#define AIMTOR_REG_TEMPERATURE                  (0x0B1BU)

/* 当前故障代码反馈寄存器。 */
#define AIMTOR_REG_FAULT_CODE                   (0x0B22U)
/* H0D_01: only qualified cold boot may clear a stopped, disabled ER.994. */
#define AIMTOR_REG_FAULT_RESET                  (0x0D01U)
#define AIMTOR_FAULT_MODBUS_TIMEOUT             (0x0994U)

/* EEPROM参数保存控制寄存器。 */
#define AIMTOR_REG_EEPROM_SAVE                  (0x0C0DU)

/* 多段位置功能总控制寄存器。 */
#define AIMTOR_REG_SEGMENT_CONTROL              (0x1100U)

/* 多段位置运行模式配置寄存器。 */
#define AIMTOR_REG_SEGMENT_MODE                 (0x1101U)

/* 多段位置段号选择寄存器。 */
#define AIMTOR_REG_SEGMENT_SELECT               (0x1104U)

/* 当前选定位置段的目标距离或位置寄存器。 */
#define AIMTOR_REG_SEGMENT_DISTANCE             (0x110CU)

/* 当前选定位置段的目标速度寄存器。 */
#define AIMTOR_REG_SEGMENT_SPEED                (0x110EU)

/* 当前选定位置段的加速度配置寄存器。 */
#define AIMTOR_REG_SEGMENT_ACCELERATION         (0x110FU)

/* 伺服当前状态寄存器。 */
#define AIMTOR_REG_SERVO_STATE                  (0x3202U)

/* 速度控制模式对应的模式值。 */
#define AIMTOR_CONTROL_MODE_SPEED               (0U)

/* 位置控制模式对应的模式值。 */
#define AIMTOR_CONTROL_MODE_POSITION            (1U)

/* 使用外部脉冲作为位置指令来源。 */
#define AIMTOR_POSITION_SOURCE_PULSE            (0U)

/* 使用内部多段位置表作为位置指令来源。 */
#define AIMTOR_POSITION_SOURCE_SEGMENT          (2U)

/* 伺服关闭或失能状态值。 */
#define AIMTOR_SERVO_OFF                        (0U)

/* 伺服开启或使能状态值。 */
#define AIMTOR_SERVO_ON                         (1U)

/*
 * AIMtor寄存器描述结构体。
 *
 * 该结构体用于描述一个寄存器的基本协议属性，
 * 包括寄存器地址、占用的数据字数量、访问功能码和缩放系数。
 *
 * 注意：
 * 该结构体只描述协议字段，不代表对应寄存器一定允许写入。
 * 实际读写仍需经过参数确认、状态检查和安全门禁。
 */
typedef struct
{
    /*
     * Modbus寄存器地址。
     *
     * 通常表示该参数或状态在AIMtor驱动器寄存器表中的起始地址。
     */
    uint16_t address;

    /*
     * 数据占用的Modbus寄存器字数量。
     *
     * 1表示占用一个16位寄存器；
     * 2表示占用两个连续的16位寄存器，通常用于32位数据。
     */
    uint8_t width_words;

    /*
     * 访问该寄存器所使用的Modbus功能码。
     *
     * 例如：
     * 03H表示读取保持寄存器；
     * 06H表示写入单个保持寄存器；
     * 10H表示写入多个保持寄存器。
     */
    uint8_t function;

    /*
     * 数据缩放系数。
     *
     * 用于将寄存器中的原始整数值转换为工程单位，
     * 或将工程单位数据转换为需要写入驱动器的原始值。
     *
     * 例如原始值乘以scale后可得到实际速度、电压或温度。
     */
    float scale;

} aimtor_register_t;

#endif
