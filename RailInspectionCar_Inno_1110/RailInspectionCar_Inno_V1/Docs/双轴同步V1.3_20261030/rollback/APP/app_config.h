#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

/* 正式配置默认只读；控制构建必须显式批准停车参数。 */
#ifndef APP_CAN_ENABLED
#define APP_CAN_ENABLED (1U)
#endif
#ifndef APP_CAN_DEBUG_ENABLED
#define APP_CAN_DEBUG_ENABLED (0U)
#endif
#ifndef APP_CAN_CONTROL_ENABLED
#define APP_CAN_CONTROL_ENABLED (1U)
#endif
#ifndef APP_CAN_STOP_PARAMETERS_APPROVED
#define APP_CAN_STOP_PARAMETERS_APPROVED (1U)
#endif
#ifndef APP_CAN_HEARTBEAT_PERIOD_MS
#define APP_CAN_HEARTBEAT_PERIOD_MS (100U)
#endif
#ifndef APP_CAN_LINK_TIMEOUT_MS
#define APP_CAN_LINK_TIMEOUT_MS (1000U)
#endif
#if (APP_CAN_ENABLED != 0U && APP_CAN_ENABLED != 1U) || \
    (APP_CAN_DEBUG_ENABLED != 0U && APP_CAN_DEBUG_ENABLED != 1U) || \
    (APP_CAN_CONTROL_ENABLED != 0U && APP_CAN_CONTROL_ENABLED != 1U) || \
    (APP_CAN_STOP_PARAMETERS_APPROVED != 0U && APP_CAN_STOP_PARAMETERS_APPROVED != 1U)
#error "CAN switches must be 0 or 1"
#endif
#if APP_CAN_CONTROL_ENABLED && (!APP_CAN_ENABLED || !APP_CAN_STOP_PARAMETERS_APPROVED)
#error "CAN motion requires transport and approved stop parameters"
#endif
#if APP_CAN_HEARTBEAT_PERIOD_MS == 0U || \
    APP_CAN_LINK_TIMEOUT_MS <= APP_CAN_HEARTBEAT_PERIOD_MS || \
    APP_CAN_LINK_TIMEOUT_MS >= 0x80000000UL
#error "Invalid CAN heartbeat timing"
#endif

#define APP_CAN_AUTH_MODE_PROTOCOL (1U)
#ifndef APP_CAN_AUTH_MODE
#define APP_CAN_AUTH_MODE APP_CAN_AUTH_MODE_PROTOCOL
#endif
#ifndef APP_CAN_SPEED_REFRESH_PERIOD_MS
#define APP_CAN_SPEED_REFRESH_PERIOD_MS (100U)
#endif
#ifndef APP_CAN_SPEED_TIMEOUT_MS
#define APP_CAN_SPEED_TIMEOUT_MS (300U)
#endif
#ifndef APP_CAN_ACQUIRE_TIMEOUT_MS
#define APP_CAN_ACQUIRE_TIMEOUT_MS (2000U)
#endif
#ifndef APP_CAN_FIRST_SPEED_TIMEOUT_MS
#define APP_CAN_FIRST_SPEED_TIMEOUT_MS (1000U)
#endif
#ifndef APP_CAN_NORMAL_STOP_DECEL_MPS2
#define APP_CAN_NORMAL_STOP_DECEL_MPS2 (0.50f)
#endif
#ifndef APP_CAN_ACCELERATION_MPS2
#define APP_CAN_ACCELERATION_MPS2 (0.15f)
#endif
#ifndef APP_CAN_DECELERATION_MPS2
#define APP_CAN_DECELERATION_MPS2 (0.30f)
#endif
#ifndef APP_CAN_REVERSE_DECEL_MPS2
#define APP_CAN_REVERSE_DECEL_MPS2 (0.30f)
#endif
#ifndef APP_CAN_LINK_STOP_DECEL_MPS2
#define APP_CAN_LINK_STOP_DECEL_MPS2 (0.50f)
#endif
#ifndef APP_CAN_QUICK_STOP_ENABLED
#define APP_CAN_QUICK_STOP_ENABLED (1U)
#endif
#ifndef APP_CAN_QUICK_STOP_DECEL_MPS2
#define APP_CAN_QUICK_STOP_DECEL_MPS2 (0.60f)
#endif
#ifndef APP_CAN_STOP_CONFIRM_MARGIN_MS
#define APP_CAN_STOP_CONFIRM_MARGIN_MS (2000U)
#endif
#ifndef APP_CAN_STOP_TIMEOUT_MAX_MS
#define APP_CAN_STOP_TIMEOUT_MAX_MS (30000U)
#endif
#ifndef APP_CAN_STOP_HOLD_ENABLED
#define APP_CAN_STOP_HOLD_ENABLED (1U)
#endif
#ifndef APP_CAN_WRITE_CONFIRM_TIMEOUT_MS
#define APP_CAN_WRITE_CONFIRM_TIMEOUT_MS (1000U)
#endif
#ifndef APP_CAN_REVERSE_CONFIRM_TIMEOUT_MS
#define APP_CAN_REVERSE_CONFIRM_TIMEOUT_MS (2000U)
#endif
#ifndef APP_CAN_CONTROL_ALLOW_REVERSE
#define APP_CAN_CONTROL_ALLOW_REVERSE (1U)
#endif
#if APP_CAN_AUTH_MODE != APP_CAN_AUTH_MODE_PROTOCOL || APP_CAN_QUICK_STOP_ENABLED > 1U
#error "Unsupported CAN authorization or quick stop policy"
#endif
#if APP_CAN_STOP_HOLD_ENABLED != 1U || APP_CAN_CONTROL_ALLOW_REVERSE > 1U || \
    APP_CAN_SPEED_TIMEOUT_MS < 100U || APP_CAN_SPEED_TIMEOUT_MS > 300U || \
    APP_CAN_SPEED_REFRESH_PERIOD_MS == 0U || APP_CAN_SPEED_REFRESH_PERIOD_MS >= APP_CAN_SPEED_TIMEOUT_MS || \
    APP_CAN_LINK_TIMEOUT_MS <= APP_CAN_SPEED_TIMEOUT_MS || APP_CAN_LINK_TIMEOUT_MS > 65534U || \
    APP_CAN_ACQUIRE_TIMEOUT_MS == 0U || APP_CAN_FIRST_SPEED_TIMEOUT_MS == 0U || \
    APP_CAN_WRITE_CONFIRM_TIMEOUT_MS == 0U || APP_CAN_REVERSE_CONFIRM_TIMEOUT_MS == 0U || \
    APP_CAN_STOP_CONFIRM_MARGIN_MS == 0U || APP_CAN_STOP_TIMEOUT_MAX_MS > 65534U
#error "Invalid CAN control timing or hold policy"
#endif

/* UART5车载屏R2控制；置0保留原只读通信，不自动授权或运动。 */
#ifndef APP_HMI_CONTROL_ENABLED
#define APP_HMI_CONTROL_ENABLED (1U)
#endif
#if (APP_HMI_CONTROL_ENABLED != 0U) && (APP_HMI_CONTROL_ENABLED != 1U)
#error "APP_HMI_CONTROL_ENABLED仅允许配置为0或1"
#endif

#define APP_HMI_ADDRESS                 (1U)
#define APP_HMI_BAUD_RATE               (115200U)
#define APP_HMI_RX_BUFFER_SIZE          (1024U)
#define APP_HMI_DMA_RX_SIZE             (512U)
#define APP_HMI_RX_EVENT_COUNT          (32U)
#define APP_HMI_RTU_QUIET_US            (1750U)
#define APP_HMI_TX_BUFFER_SIZE          (256U)
#define APP_HMI_SESSION_LEASE_MS        (1000U)
#define APP_HMI_HEARTBEAT_PERIOD_MS     (200U)
#define APP_HMI_SPEED_HOLD_MAX_MS       (500U)
#define APP_HMI_WRITE_TIMEOUT_MS        (1000U)
#define APP_HMI_TASK_PERIOD_MS          (2U)
#define APP_HMI_MAX_FRAME_SIZE          (256U)
#define APP_HMI_DICTIONARY_REVISION     (2U)
#define APP_HMI_MAILBOX_START           (0x0100U)
#define APP_HMI_MAILBOX_REGISTERS      (24U)
#define APP_HMI_RESULT_START            (0x0120U)
#define APP_HMI_RESULT_REGISTERS       (20U)
#define APP_HMI_METADATA_START          (0x0200U)
#define APP_HMI_METADATA_REGISTERS     (24U)
#define APP_HMI_SNAPSHOT_PERIOD_MS     (50U)
#define APP_HMI_DETAIL_START           (0x0300U)
#define APP_HMI_DETAIL_REGISTERS       (352U)
#define APP_HMI_FW_VERSION             (0x0100U)
#define APP_HMI_FW_PATCH               (1U)

/*
 * 机械参数配置。
 * 后续进行轮径、减速比或脉冲标定时，原则上只修改本区域。
 */

/* 车轮实际直径，单位：米。当前轮径为185 mm。 */
#define APP_WHEEL_DIAMETER_M \
    (0.185f)

/* 减速机减速比，定义为电机转速/减速机输出轴转速。 */
#define APP_REDUCER_REDUCTION_RATIO \
    (3.0f)
#define APP_REDUCER_REDUCTION_NUMERATOR  (3)
#define APP_REDUCER_REDUCTION_DENOMINATOR (1)

/* 链传动齿数；大链轮安装在减速机输出轴，小链轮安装在钢轮轴。 */
#define APP_CHAIN_LARGE_SPROCKET_TEETH \
    (18)
#define APP_CHAIN_SMALL_SPROCKET_TEETH \
    (16)

#define APP_GEAR_REDUCTION_NUMERATOR \
    (APP_REDUCER_REDUCTION_NUMERATOR * APP_CHAIN_SMALL_SPROCKET_TEETH)
#define APP_GEAR_REDUCTION_DENOMINATOR \
    (APP_REDUCER_REDUCTION_DENOMINATOR * APP_CHAIN_LARGE_SPROCKET_TEETH)
#define APP_WHEEL_CIRCUMFERENCE_MM_INTEGER (581)

#if (APP_REDUCER_REDUCTION_NUMERATOR <= 0) || \
    (APP_REDUCER_REDUCTION_DENOMINATOR <= 0) || \
    (APP_GEAR_REDUCTION_NUMERATOR <= 0) || \
    (APP_GEAR_REDUCTION_DENOMINATOR <= 0)
#error "减速机和总传动比整数参数必须为正"
#endif

#if (APP_CHAIN_LARGE_SPROCKET_TEETH <= 0) || \
    (APP_CHAIN_SMALL_SPROCKET_TEETH <= 0)
#error "链轮齿数必须为正"
#endif

/* 电机到最终钢轮的总传动比，定义为电机转速/钢轮转速。 */
#define APP_GEAR_REDUCTION_RATIO \
    (APP_REDUCER_REDUCTION_RATIO * \
     (float)APP_CHAIN_SMALL_SPROCKET_TEETH / \
     (float)APP_CHAIN_LARGE_SPROCKET_TEETH)

/* 电机或驱动器输出一整圈对应的脉冲数。 */
#define APP_PULSES_PER_REVOLUTION \
    (10000.0f)

/* 车轮周长，单位：米。计算公式为π×轮径。 */
#define APP_WHEEL_CIRCUMFERENCE_M \
    (3.14159265358979323846f * APP_WHEEL_DIAMETER_M)

/* 配置换算使用双精度中间量，避免ARMCC隐式提升单精度参数产生警告。 */
#define APP_WHEEL_CIRCUMFERENCE_M_D \
    (3.14159265358979323846 * (double)APP_WHEEL_DIAMETER_M)

/*
 * 每米对应的脉冲数，单位：pulse/m。
 * 计算公式：
 * 每转脉冲数×减速比÷车轮周长。
 */
#define APP_PULSES_PER_METER \
    (APP_PULSES_PER_REVOLUTION * APP_GEAR_REDUCTION_RATIO / \
     APP_WHEEL_CIRCUMFERENCE_M)

/*
 * 设计最大线速度，单位：m/s。
 * 统一按1600rpm与当前机械参数计算，约20.92km/h。
 * 该值属于设计参数，不代表第一阶段允许直接测试的速度。
 */
#define APP_DESIGN_MAX_VELOCITY_MPS \
    (APP_OPERATION_MAX_MOTOR_RPM * APP_WHEEL_CIRCUMFERENCE_M / \
     (60.0f * APP_GEAR_REDUCTION_RATIO))

/* 正式车辆运行限速，单位为m/s，与共同目标转速上限一致。 */
#define APP_VEHICLE_MAX_VELOCITY_MPS \
    APP_DESIGN_MAX_VELOCITY_MPS

/* 正式车辆运行限速，单位为mm/s，取当前机械参数对应整数下限。 */
#define APP_OPERATION_MAX_VELOCITY_MM_S \
    ((int32_t)(APP_OPERATION_MAX_MOTOR_RPM * APP_WHEEL_CIRCUMFERENCE_M_D * 1000.0 / \
     (60.0 * (double)APP_GEAR_REDUCTION_RATIO)))

/* 电机/驱动器绝对保护上限，不能直接作为车辆运行速度上限。 */
#define APP_DEBUG_MAX_RPM \
    (1600)

/* 所有控制源统一的电机轴运行上限，单位为rpm。 */
#define APP_OPERATION_MAX_MOTOR_RPM (1600)

#if (APP_OPERATION_MAX_MOTOR_RPM <= 0) || \
    (APP_OPERATION_MAX_MOTOR_RPM > APP_DEBUG_MAX_RPM)
#error "车辆运行电机转速上限必须位于电机绝对保护上限以内"
#endif

/* 设计最大加速度，单位：m/s²。 */
#ifndef APP_DESIGN_ACCELERATION_MPS2
#define APP_DESIGN_ACCELERATION_MPS2 \
    (0.15)
#endif

/* 设计最大减速度，单位：m/s²。 */
#ifndef APP_DESIGN_DECELERATION_MPS2
#define APP_DESIGN_DECELERATION_MPS2 \
    (0.30f)
#endif

/* RS485运行反馈持续异常后的受控停车减速度，单位：m/s²。 */
#ifndef APP_SERVO_SAFETY_STOP_DECEL_MPS2
#define APP_SERVO_SAFETY_STOP_DECEL_MPS2 (0.60f)
#endif


/*
 * 左轴调试运动参数。
 * 保留频率和转速上限，不设置单次0.1米行程限制。
 */

/*
 * 允许输出的最小脉冲频率，单位：Hz。
 *
 * 按当前项目调试要求，外部脉冲命令最低输出100 Hz。
 * 这是软件配置下限，不将速度环响应带宽解释为输入脉冲最低频率。
 */
#define APP_PULSE_MIN_HZ \
    (100U)

/*
 * 允许输出的最大脉冲频率，单位：Hz。
 * 当前限制为100 kHz；新链轮传动下外部脉冲模式约覆盖7.85 km/h。
 */
#define APP_PULSE_MAX_HZ \
    (100000U)

/*
 * 第一阶段调试允许的最大转速，单位：rpm。
 *
 * 电机/驱动器绝对保护上限为1750 rpm；正式车辆运行仍受派生电机转速上限约束。
 * 外部脉冲模式另外受APP_PULSE_MAX_HZ限制。
 */
/*
 *
 * 根据《MD130AIS_RS485_Modbus_RTU控制说明.md》：
 * 速度 = 脉冲频率 × 60 / 每圈脉冲数；
 * 当前电子齿轮确认每圈10000脉冲；外部脉冲模式固定限制为100000 Hz。
 * 这里保留电机轴与车辆线速度的换算，避免速度模式和脉冲模式混淆。
 */
#define APP_DEBUG_MAX_VELOCITY_MPS \
    ((float)APP_DEBUG_MAX_RPM * APP_WHEEL_CIRCUMFERENCE_M / \
     (60.0f * APP_GEAR_REDUCTION_RATIO))

/*
 * 第一阶段调试允许的最大脉冲频率，单位：Hz。
 * 当前工程固定为100000 Hz，不随RS485速度模式的车辆硬限速提高。
 */
#define APP_DEBUG_MAX_PULSE_HZ \
    (APP_PULSE_MAX_HZ)

/*
 * 第一阶段默认加速度，单位：m/s²。
 * 该值用于运动规划器生成平滑的加速曲线。
 */
#define APP_DEFAULT_ACCELERATION_MPS2 \
    APP_DESIGN_ACCELERATION_MPS2

/*
 * 脉冲模式加速度，单位：rpm/s。
 * 该参数只用于STM32外部脉冲模式，不影响RS485速度模式。
 */
#define APP_PULSE_ACCELERATION_RPM_PER_S \
    (20.0f)

/*
 * 脉冲模式减速度，单位：rpm/s。
 * 该参数只用于STM32外部脉冲模式，不影响RS485速度模式。
 */
#define APP_PULSE_DECELERATION_RPM_PER_S \
    (100.0f)

/*
 * 将脉冲模式转速变化率换算为线加速度，供现有m/s运动规划器使用。
 */
#define APP_PULSE_ACCELERATION_MPS2 \
    (APP_PULSE_ACCELERATION_RPM_PER_S * APP_WHEEL_CIRCUMFERENCE_M / \
     (60.0f * APP_GEAR_REDUCTION_RATIO))

#define APP_PULSE_DECELERATION_MPS2 \
    (APP_PULSE_DECELERATION_RPM_PER_S * APP_WHEEL_CIRCUMFERENCE_M / \
     (60.0f * APP_GEAR_REDUCTION_RATIO))

/*
 * 脉冲模式加减速率，单位：Hz/s。
 * 当前10000 pulse/rev下分别约为3333.33 Hz/s和33333.33 Hz/s。
 */
#define APP_PULSE_ACCELERATION_HZ_PER_S \
    (APP_PULSE_ACCELERATION_RPM_PER_S * APP_PULSES_PER_REVOLUTION / 60.0f)

#define APP_PULSE_DECELERATION_HZ_PER_S \
    (APP_PULSE_DECELERATION_RPM_PER_S * APP_PULSES_PER_REVOLUTION / 60.0f)

/*
 * 驱动器斜坡时间参数，单位：ms。
 *
 * 含义：
 * 从0 rpm加速到1000 rpm所需要的时间。
 *
 * 计算过程：
 * 1. 根据车轮周长、减速比和默认加速度计算达到1000 rpm所需时间；
 * 2. 转换为毫秒；
 * 3. 限制在16位无符号整数范围内。
 *
 * 如果计算结果超过65535 ms，则使用65535 ms。
 */
#define APP_SERVO_RAMP_MS                                                   \
    ((uint16_t)(                                                           \
        (1000.0f * APP_WHEEL_CIRCUMFERENCE_M /                            \
         (60.0f * APP_GEAR_REDUCTION_RATIO *                              \
          APP_DEFAULT_ACCELERATION_MPS2) * 1000.0f) > 65535.0f             \
            ? 65535.0f                                                     \
            : (1000.0f * APP_WHEEL_CIRCUMFERENCE_M /                       \
               (60.0f * APP_GEAR_REDUCTION_RATIO *                         \
                APP_DEFAULT_ACCELERATION_MPS2) * 1000.0f)))

/*
 * RS485速度模式的驱动器内部加减速时间，单位：ms。
 * 正常运动由主控软件斜坡负责；H06_05仅保留最小驱动器保护时间。
 * H06_06继续按全部受控停车参数的最大值配置，独立安全通道仍可使用。
 */
#define APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS \
    (1000000.0 * APP_WHEEL_CIRCUMFERENCE_M_D / \
     (60.0 * (double)APP_GEAR_REDUCTION_RATIO * \
      (double)APP_DESIGN_ACCELERATION_MPS2))
#define APP_SERVO_DIRECT_ACCELERATION_RAMP_MS \
    ((uint32_t)(APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS + 0.5))
/* RAW保留设计加速度对应的理论时间，MS表示实际写入H06_05的值。 */
#ifndef APP_SERVO_DRIVER_ACCEL_GUARD_MS
#define APP_SERVO_DRIVER_ACCEL_GUARD_MS (1U)
#endif
#if APP_SERVO_DRIVER_ACCEL_GUARD_MS < 1U || APP_SERVO_DRIVER_ACCEL_GUARD_MS > 65535U
#error "Driver acceleration guard must be 1..65535 ms per 1000 rpm"
#endif
#define APP_SERVO_SPEED_ACCELERATION_RAMP_MS APP_SERVO_DRIVER_ACCEL_GUARD_MS

/* 分级停车配置；所有线速度减速度单位m/s²。 */
#define APP_HMI_NORMAL_STOP_DECEL_MPS2 (0.50f)
#define APP_HMI_LINK_STOP_DECEL_MPS2 (0.50f)
#define APP_HMI_EMERGENCY_STOP_DECEL_MPS2 (0.60f)
#define APP_HMI_NORMAL_STOP_DECEL_MM_S2 ((uint16_t)(APP_HMI_NORMAL_STOP_DECEL_MPS2 * 1000.0f + 0.5f))
#define APP_HMI_LINK_STOP_DECEL_MM_S2 ((uint16_t)(APP_HMI_LINK_STOP_DECEL_MPS2 * 1000.0f + 0.5f))
#define APP_HMI_EMERGENCY_STOP_DECEL_MM_S2 ((uint16_t)(APP_HMI_EMERGENCY_STOP_DECEL_MPS2 * 1000.0f + 0.5f))
#define APP_TASK_STOP_DECEL_MPS2 (0.80f)
#define APP_DRIVER_COMM_TIMEOUT_MS (1000U)
#define APP_COLD_CLEAR_ONLINE_TIMEOUT_MS (30000U)
#define APP_COLD_CLEAR_DEADLINE_MS (10000U)
#define APP_COLD_CLEAR_FEEDBACK_MAX_MS (500U)
#define APP_DRIVER_BOOT_SILENCE_MS (1200U)
#define APP_DRIVER_NO2_STOP_MODE (2U)
#define APP_DRIVER_MIN_FIRMWARE (558U)
/* 生产默认只开放已纳入独立停车保护的内部速度模式。 */
#ifndef APP_UNPROTECTED_MOTION_TEST_ENABLED
#define APP_UNPROTECTED_MOTION_TEST_ENABLED (0U)
#endif
#if APP_UNPROTECTED_MOTION_TEST_ENABLED != 0U && APP_UNPROTECTED_MOTION_TEST_ENABLED != 1U
#error "APP_UNPROTECTED_MOTION_TEST_ENABLED must be 0 or 1"
#endif
#define APP_TASK_STOP_RAMP_MS ((uint32_t)(1000000.0 * APP_WHEEL_CIRCUMFERENCE_M_D / \
    (60.0 * (double)APP_GEAR_REDUCTION_RATIO * (double)APP_TASK_STOP_DECEL_MPS2) + 0.5))

#define APP_MOTION_MAX_RATE(first, second) \
    ((first) > (second) ? (first) : (second))
#define APP_SERVO_SPEED_MAX_DECELERATION_MPS2 \
    APP_MOTION_MAX_RATE(APP_TASK_STOP_DECEL_MPS2, \
    APP_MOTION_MAX_RATE(APP_SERVO_SAFETY_STOP_DECEL_MPS2, \
        APP_MOTION_MAX_RATE(APP_CAN_QUICK_STOP_DECEL_MPS2, \
            APP_MOTION_MAX_RATE(APP_DESIGN_DECELERATION_MPS2, \
                APP_MOTION_MAX_RATE(APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2, \
                    APP_MOTION_MAX_RATE(APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2, \
                        APP_MOTION_MAX_RATE(APP_CAN_NORMAL_STOP_DECEL_MPS2, \
                                            APP_CAN_LINK_STOP_DECEL_MPS2)))))))
#define APP_SERVO_SPEED_DECELERATION_RAMP_RAW_MS \
    (1000000.0 * APP_WHEEL_CIRCUMFERENCE_M_D / \
     (60.0 * (double)APP_GEAR_REDUCTION_RATIO * \
      (double)APP_SERVO_SPEED_MAX_DECELERATION_MPS2))
#define APP_SERVO_SPEED_DECELERATION_RAMP_MS \
    ((uint32_t)(APP_SERVO_SPEED_DECELERATION_RAMP_RAW_MS + 0.5))

/* 保留旧名称，供只读诊断和外部兼容代码表示最快驱动器斜坡。 */
#define APP_SERVO_SPEED_RAMP_MS APP_SERVO_SPEED_DECELERATION_RAMP_MS

/* 独立CLI位置段保留原有默认值，不跟随底盘速度模式参数变化。 */
#define APP_SERVO_DEBUG_POSITION_RAMP_MS (100U)

#define APP_MOTION_RAMP_MAX_DT_MS (50U)
#if APP_MOTION_RAMP_MAX_DT_MS == 0U
#error "斜坡积分时间上限必须为正"
#endif


/*
 * 脉冲定时器计数频率，单位：Hz。
 * 当前配置为500 kHz，定时器根据该计数基准计算ARR和CCR。
 */
#define APP_PULSE_TIMER_TICK_HZ \
    (500000UL)

/*
 * 方向切换前后的稳定等待时间，单位：ms。
 * 方向切换必须先停止PWM，再修改方向，最后等待该时间后重新输出脉冲。
 */
#define APP_DIRECTION_SETTLE_MS \
    (2U)


/*
 * RS485和Modbus通信参数。
 * USART2和两台驱动器统一配置为115200、8N1，升级后需进行实机回归。
 */

/* USART2 RS485通信波特率，单位：bit/s。 */
#define APP_RS485_BAUD_RATE \
    (115200U)

/*
 * USART2接收DMA硬件缓冲区大小，单位：字节。
 * 当前使用256字节Circular DMA缓冲区。
 */
#define APP_RS485_DMA_RX_SIZE \
    (256U)

/*
 * RS485软件接收环形缓冲区大小，单位：字节。
 * DMA接收到的数据会被复制到该软件环形缓冲区。
 */
#define APP_RS485_RING_SIZE \
    (512U)

/*
 * Modbus RTU允许处理的最大帧长度，单位：字节。
 * 超过该长度的数据帧应被判定为非法或异常帧。
 */
#define APP_MODBUS_MAX_FRAME_SIZE \
    (256U)

/*
 * Modbus帧间隔判定时间，单位：ms。
 * 波特率升级后保留2 ms间隔，不随波特率翻倍而缩短。
 */
#define APP_MODBUS_FRAME_GAP_MS \
    (2U)

/*
 * Modbus从站响应超时时间，单位：ms。
 * 超过该时间没有收到有效响应，则本次事务判定为超时。
 */
#define APP_MODBUS_RESPONSE_TIMEOUT_MS \
    (50U)

/*
 * Modbus请求最大重试次数。
 * 当前配置为最多重试2次。
 */
#define APP_MODBUS_MAX_RETRIES \
    (2U)

/*
 * 左侧伺服驱动器的Modbus从站地址。
 */
#define APP_SERVO_LEFT_SLAVE_ADDRESS \
    (1U)

/*
 * 右侧伺服驱动器的Modbus从站地址。
 */
#define APP_SERVO_RIGHT_SLAVE_ADDRESS \
    (2U)

/*
 * 判定伺服离线所需的连续通信失败次数。
 * 达到该次数后，对应轴可被标记为OFFLINE。
 */
#define APP_SERVO_OFFLINE_FAILURES \
    (3U)

/* 运行中关键反馈允许的连续通信事务失败次数。 */
#define APP_SERVO_RUNTIME_COMM_FAILURES \
    (5U)

/*
 * 伺服快速状态轮询周期，单位：ms。
 * 用于较高频率读取伺服状态、故障和实际速度等信息。
 */
#define APP_SERVO_FAST_POLL_MS \
    (20U)

/*
 * 伺服慢速参数轮询周期，单位：ms。
 * 用于读取不需要高频更新的配置或诊断参数。
 */
#define APP_SERVO_SLOW_POLL_MS \
    (1000U)

/*
 * 伺服状态快照允许的最大年龄，单位：ms。
 * 超过该时间未更新有效状态时，状态快照可能被视为过期。
 */
#define APP_SERVO_STATUS_MAX_AGE_MS \
    (500U)

/*
 * 位置反馈允许的最大年龄，单位：ms。
 * 位置寄存器属于后台查询，周期为1000 ms，因此不能直接复用500 ms的
 * 速度/故障关键状态年龄。位置模式完成判定仍要求收到本次命令后的新样本。
 */
#define APP_SERVO_POSITION_MAX_AGE_MS \
    (1500U)

/*
 * RS485内部位置模式允许的目标位置误差，单位：脉冲。
 * 该值用于判断实际位置是否到达本次命令的目标位置。
 */
#define APP_SERVO_POSITION_TOLERANCE_PULSES \
    (2L)

/*
 * 定脉冲输出关闭后，等待驱动器实际速度归零的最长时间，单位：ms。
 * 超时只报告完成确认故障，不自动失能，避免把正常完成误判为急停。
 */
#define APP_PULSE_COMPLETION_STOP_TIMEOUT_MS \
    (3000U)

/* 定脉冲完成阶段的编码器位置确认轮询周期，单位：ms。 */
#define APP_PULSE_COMPLETION_POSITION_POLL_MS \
    (20U)

/* 到位后要求连续保持不变的位置反馈样本数。 */
#define APP_PULSE_COMPLETION_STABLE_SAMPLES \
    (3U)

/* 编译期选择伺服只读查询的轴范围。 */
#define APP_SERVO_QUERY_LEFT_ONLY  (1U)
#define APP_SERVO_QUERY_RIGHT_ONLY (2U)
#define APP_SERVO_QUERY_BOTH       (3U)

/* 查询轴范围。 */
#ifndef APP_SERVO_QUERY_MODE
#define APP_SERVO_QUERY_MODE APP_SERVO_QUERY_BOTH
#endif
#ifndef APP_SERVO_READONLY_TEST
#define APP_SERVO_READONLY_TEST (0U)
#endif
#if APP_CAN_CONTROL_ENABLED && APP_SERVO_READONLY_TEST
#error "CAN motion is forbidden in servo read-only configuration"
#endif

/* 控制轴掩码：bit0为左轴，bit1为右轴。 */
#define APP_SERVO_CONTROL_AXIS_LEFT  (1U)
#define APP_SERVO_CONTROL_AXIS_RIGHT (2U)
#define APP_SERVO_CONTROL_AXIS_BOTH  (3U)

#ifndef APP_SERVO_CONTROL_AXIS
#define APP_SERVO_CONTROL_AXIS APP_SERVO_CONTROL_AXIS_BOTH
#endif

/* 关键状态、后台诊断和离线探测的目标周期，单位：ms。 */
#define APP_SERVO_CRITICAL_POLL_MS   (20U)
#define APP_SERVO_BACKGROUND_POLL_MS (250U)
#define APP_SERVO_BACKGROUND_MAX_CRITICAL (4U)
#define APP_SERVO_OFFLINE_PROBE_MS   (1000U)
#define APP_SERVO_DIAGNOSTIC_POLL_MS (1000U)
#define APP_SERVO_DIAGNOSTIC_RETRY_MS (200U)

/* 只读轮询不立即重试，避免缺席轴连续占用通信窗口。 */
#define APP_SERVO_POLL_RETRIES (0U)

/*
 * 遥控器速度控制参数。
 *
 * LV输入范围为0~2000；940~1060为中位死区。
 * 速度控制使用当前编译配置选择的控制轴；RIGHT_ONLY时仅控制右轴，
 * BOTH时向左右轴下发相同目标转速。
 */
/* 协议仍按物理键编号累计事件，索引从零开始。 */
#define APP_REMOTE_STOP_AUTH_KEY_INDEX          (4U)
#define APP_REMOTE_WATCHDOG_KEY_INDEX           (5U)
#define APP_REMOTE_SERVO_ENABLE              (1U)
#define APP_REMOTE_SERVO_MIN_RPM             (-APP_OPERATION_MAX_MOTOR_RPM)
#define APP_REMOTE_SERVO_MAX_RPM             (APP_OPERATION_MAX_MOTOR_RPM)
#define APP_REMOTE_SERVO_NEUTRAL_LOW         (940U)
#define APP_REMOTE_SERVO_NEUTRAL_HIGH        (1060U)
#define APP_REMOTE_SERVO_LINK_TIMEOUT_MS     (150U)
#define APP_REMOTE_SERVO_UPDATE_PERIOD_MS    (10U)

/* 失联和K5停车独立整定，不改变正常调速斜坡；单位：m/s²。 */
#ifndef APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2
#define APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 (0.50f)
#endif
/* 物理K5负责停车和授权；旧K1参数名只作为源码兼容入口。 */
#if defined(APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2) && defined(APP_REMOTE_SERVO_K1_STOP_DECEL_MPS2)
#error "K5停车减速度的新旧宏不能同时定义"
#endif
#ifndef APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2
#ifdef APP_REMOTE_SERVO_K1_STOP_DECEL_MPS2
#define APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 APP_REMOTE_SERVO_K1_STOP_DECEL_MPS2
#else
#define APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 (0.60f)
#endif
#endif
#ifndef APP_REMOTE_SERVO_K1_STOP_DECEL_MPS2
#define APP_REMOTE_SERVO_K1_STOP_DECEL_MPS2 APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2
#endif
#define APP_REMOTE_SERVO_K5_STOP_DECEL_MM_S2 \
    ((uint16_t)(APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 * 1000.0f + 0.5f))
#define APP_REMOTE_SERVO_LINK_STOP_DECEL_MM_S2 \
    ((uint16_t)(APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 * 1000.0f + 0.5f))
/* 停车批次计算间隔及单次时间上限，单位：ms；延误不追补大步长。 */
#define APP_REMOTE_SERVO_STOP_UPDATE_PERIOD_MS (20U)
#define APP_REMOTE_SERVO_STOP_MAX_DT_MS        (50U)
/* 停车反馈确认余量，不复用启动待机期限；单位：ms。 */
#define APP_REMOTE_SERVO_STOP_CONFIRM_MARGIN_MS (2000U)
#if (APP_REMOTE_SERVO_STOP_UPDATE_PERIOD_MS == 0U) || \
    (APP_REMOTE_SERVO_STOP_MAX_DT_MS < APP_REMOTE_SERVO_STOP_UPDATE_PERIOD_MS)
#error "停车计算周期必须为正且不超过单次时间上限"
#endif
/* 浮点配置在停车入口检查正值和有限性，非法值只能进入原急停路径。 */

#ifndef APP_SERVO_CLI_DEBUG_ENABLED
/* 独立串口调试授权开关，关闭时仍允许查询和安全停止。 */
#define APP_SERVO_CLI_DEBUG_ENABLED          (0U)
#endif
#ifndef APP_SERVO_CLI_DEBUG_TIMEOUT_MS
/* 只有匹配会话号的保活刷新期限，单位：ms。 */
#define APP_SERVO_CLI_DEBUG_TIMEOUT_MS       (180000U)
#endif
#if (APP_SERVO_CLI_DEBUG_ENABLED > 1U)
#error "CLI调试开关仅允许0或1"
#endif
#if (APP_SERVO_CLI_DEBUG_TIMEOUT_MS == 0U) || \
    (APP_SERVO_CLI_DEBUG_TIMEOUT_MS > 2147483647U)
#error "CLI保活期限必须为正且小于半个32位计时范围"
#endif

/* 各类复位安全流程完成后是否进入伺服使能零速待机。 */
#ifndef APP_SERVO_STARTUP_HOLD_ENABLED
#define APP_SERVO_STARTUP_HOLD_ENABLED       (1U)
#endif
/* 待机模式准备和反馈确认的总时限，单位：ms。 */
#define APP_SERVO_STARTUP_HOLD_TIMEOUT_MS    (10000U)
#if (APP_SERVO_STARTUP_HOLD_ENABLED > 1U)
#error "伺服上电使能待机开关仅允许配置为0或1"
#endif

/* 双轴实际速度同步开关：修改后重新编译下载，仅双轴遥控生效。 */
#ifndef APP_REMOTE_SERVO_SYNC_ENABLED
#define APP_REMOTE_SERVO_SYNC_ENABLED (1U)
#endif
#ifndef APP_REMOTE_SERVO_SYNC_TRIP_ENABLED
/* 双轴实际速度同步超差联停开关：0仅报警，1超差持续达到阈值时联停。 */
#define APP_REMOTE_SERVO_SYNC_TRIP_ENABLED (0U)
#endif
#define APP_REMOTE_SERVO_SYNC_KP             (0.30f)
#define APP_REMOTE_SERVO_SYNC_PERIOD_MS      (50U)
#define APP_REMOTE_SERVO_SYNC_MAX_AGE_MS     (100U)
/* 双轴速度反馈样本允许的最大时间差，单位：ms。 */
#define APP_REMOTE_SERVO_SYNC_MAX_SKEW_MS (40U)
#define APP_REMOTE_SERVO_SYNC_LIMIT_RPM      (5.0f)
/* 实际速度差不扣除死区，直接参与P补偿。 */
#define APP_REMOTE_SERVO_SYNC_DEADBAND_RPM (1)
#define APP_REMOTE_SERVO_SYNC_RATE_RPM_S     (20.0f)
#define APP_REMOTE_SERVO_SYNC_MIN_RPM        (10)
#define APP_REMOTE_SERVO_SYNC_ERROR_RPM      (30)
#define APP_REMOTE_SERVO_SYNC_ERROR_MS       (300U)
#define APP_REMOTE_SERVO_SYNC_REVERSE_MS     (2000U)
#ifndef APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED
/* 双轴实际速度同步CLI调试输出开关：0关闭，1启用。 */
#define APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED (0U)
#endif
/* 双轴实际速度同步CLI调试输出周期，单位：ms。 */
#define APP_REMOTE_SERVO_SYNC_DEBUG_PERIOD_MS (100U)
#if (APP_REMOTE_SERVO_SYNC_ENABLED > 1U) || (APP_REMOTE_SERVO_SYNC_TRIP_ENABLED > 1U)
#error "同步开关仅允许0或1"
#endif
#if (APP_REMOTE_SERVO_SYNC_DEBUG_ENABLED > 1U)
#error "双轴实际速度同步CLI调试开关仅允许配置为0或1"
#endif

/* 位置与轮转里程监控：只采集诊断，不参与运动控制。 */
#ifndef APP_POSITION_MONITOR_ENABLED
#define APP_POSITION_MONITOR_ENABLED (1U)
#endif
#define APP_POSITION_MAX_AGE_MS       (1500U)
#define APP_POSITION_MAX_SKEW_MS      (200U)
#define APP_POSITION_JUMP_MARGIN      (100U)
/* 反馈单位独立于外部输入脉冲，已实测每电机转10000计数。 */
#define APP_POSITION_LEFT_COUNTS_PER_REV  (10000U)
#define APP_POSITION_RIGHT_COUNTS_PER_REV (10000U)
#define APP_POSITION_LEFT_SIGN            (1)
#define APP_POSITION_RIGHT_SIGN           (1)
/* 用整数机械参数进行编译期合法性检查，传动比为电机转数/车轮转数。 */
#define APP_POSITION_LEFT_WHEEL_DIAMETER_MM  (185U)
#define APP_POSITION_RIGHT_WHEEL_DIAMETER_MM (185U)
#define APP_POSITION_LEFT_GEAR_NUM           (8U)
#define APP_POSITION_LEFT_GEAR_DEN           (3U)
#define APP_POSITION_RIGHT_GEAR_NUM          (8U)
#define APP_POSITION_RIGHT_GEAR_DEN          (3U)
#if (APP_POSITION_MONITOR_ENABLED != 0U) && (APP_POSITION_MONITOR_ENABLED != 1U)
#error "位置监控开关仅允许0或1"
#endif
#if ((APP_POSITION_LEFT_SIGN != 1) && (APP_POSITION_LEFT_SIGN != -1)) || \
    ((APP_POSITION_RIGHT_SIGN != 1) && (APP_POSITION_RIGHT_SIGN != -1))
#error "位置反馈方向仅允许+1或-1"
#endif
#if (APP_POSITION_LEFT_COUNTS_PER_REV <= 0) || \
    (APP_POSITION_RIGHT_COUNTS_PER_REV <= 0) || \
    (APP_POSITION_LEFT_WHEEL_DIAMETER_MM <= 0) || \
    (APP_POSITION_RIGHT_WHEEL_DIAMETER_MM <= 0) || \
    (APP_POSITION_LEFT_GEAR_NUM <= 0) || (APP_POSITION_LEFT_GEAR_DEN <= 0) || \
    (APP_POSITION_RIGHT_GEAR_NUM <= 0) || (APP_POSITION_RIGHT_GEAR_DEN <= 0)
#error "位置监控机械参数必须为正"
#endif

/* 遥控速度控制允许的连续通信异常次数。 */
#define APP_REMOTE_SERVO_COMM_FAILURE_LIMIT  (3U)

/* 伺服通信任务调度周期，避免高优先级任务无阻塞占满CPU。 */
#define APP_SERVO_COMM_PERIOD_MS            (1U)

/* NRF24L01接收任务调度周期，遥控器发送周期为10ms。 */
#define APP_NRF_TASK_PERIOD_MS              (5U)

/* 配套六页遥控固件启用回传；旧遥控器须同时将以下两个开关改为0。 */
#ifndef APP_NRF_TELEMETRY_ENABLED
/*
控制主控是否生成并发送状态数据帧：
- 0U：关闭主控状态回传，不生成0x81~0x87状态帧。
- 1U：开启主控状态回传，生成伺服状态、速度、位置、里程、同步和告警数据。
*/
#define APP_NRF_TELEMETRY_ENABLED           (1U)
#endif
#ifndef APP_NRF_ACK_PAYLOAD_ENABLED
/*
控制NRF24L01是否启用ACK Payload能力：
- 0U：关闭ACK Payload。
- 1U：启用动态Payload和ACK Payload，用于在遥控器控制帧的应答中携带主控状态。
*/
#define APP_NRF_ACK_PAYLOAD_ENABLED         (1U)
#endif
#define APP_NRF_SPI_TRANSACTION_TIMEOUT_MS  (2U)
#define APP_NRF_RECOVERY_BACKOFF_MS         (500U)
/* 配置巡检分事务执行，不能挤占控制帧和状态回传。 */
#define APP_NRF_CONFIG_CHECK_PERIOD_MS      (1000U)
#define APP_NRF_CONFIG_CHECK_SLOT_MS        (20U)
/* 首ACK和运行中ACK装载均有界等待，不以装载成功冒充远端收到。 */
#define APP_NRF_ACK_LOAD_TIMEOUT_MS         (100U)
#define APP_NRF_FAST_STATUS_PERIOD_MS       (100U)
#define APP_NRF_DIAGNOSTIC_PERIOD_MS        (200U)
#define APP_NRF_POSITION_PERIOD_MS          (250U)
#define APP_NRF_PARAMETER_PERIOD_MS         (5000U)
#define APP_NRF_ALARM_PERIOD_MS             (1000U)
#define APP_NRF_SERVO_DIAGNOSTIC_PERIOD_MS  (500U)
#define APP_NRF_FAST_STATUS_MAX_BURST      (2U)
#define APP_NRF_FAST_STATUS_URGENT_AGE_MS  (200U)
#ifndef APP_NRF_DIAGNOSTIC_ENABLED
/* 仅控制RF_*周期串口诊断打印，不关闭遥控、状态回传或失联保护。 */
#define APP_NRF_DIAGNOSTIC_ENABLED (1U)
#endif
#if APP_NRF_DIAGNOSTIC_ENABLED > 1U
#error "APP_NRF_DIAGNOSTIC_ENABLED must be 0 or 1"
#endif
#ifndef APP_NRF_BUILD_ID
#define APP_NRF_BUILD_ID "lcdrel-rev-20261029-" __DATE__ "-" __TIME__
#endif
#ifndef APP_NRF_VEHICLE_SPEED_STATUS_ENABLED
#define APP_NRF_VEHICLE_SPEED_STATUS_ENABLED (1U)
#endif

#if (APP_NRF_TELEMETRY_ENABLED > 1U) || (APP_NRF_ACK_PAYLOAD_ENABLED > 1U) || \
    (APP_NRF_VEHICLE_SPEED_STATUS_ENABLED > 1U)
#error "NRF状态回传和ACK载荷开关仅允许0或1"
#endif
#if APP_NRF_TELEMETRY_ENABLED && !APP_NRF_ACK_PAYLOAD_ENABLED
#error "NRF状态回传必须同时启用ACK载荷"
#endif

#if (APP_OPERATION_MAX_MOTOR_RPM <= 0) || \
    (APP_REMOTE_SERVO_MIN_RPM != -APP_OPERATION_MAX_MOTOR_RPM) || \
    (APP_REMOTE_SERVO_MAX_RPM != APP_OPERATION_MAX_MOTOR_RPM)
#error "遥控器速度范围必须位于车辆运行上限以内"
#endif

#if (APP_REMOTE_SERVO_NEUTRAL_LOW >= APP_REMOTE_SERVO_NEUTRAL_HIGH)
#error "遥控器LV中位死区配置无效"
#endif

#if ((APP_SERVO_QUERY_MODE != APP_SERVO_QUERY_LEFT_ONLY) && \
     (APP_SERVO_QUERY_MODE != APP_SERVO_QUERY_RIGHT_ONLY) && \
     (APP_SERVO_QUERY_MODE != APP_SERVO_QUERY_BOTH))
#error "APP_SERVO_QUERY_MODE必须为1、2或3"
#endif

#if ((APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_LEFT) && \
     (APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_RIGHT) && \
     (APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_BOTH))
#error "APP_SERVO_CONTROL_AXIS必须为左轴、右轴或双轴掩码"
#endif

#if ((APP_SERVO_CONTROL_AXIS & APP_SERVO_CONTROL_AXIS_LEFT) != 0U) && \
    (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY)
#error "左轴控制必须同时选择左轴查询"
#endif

#if ((APP_SERVO_CONTROL_AXIS & APP_SERVO_CONTROL_AXIS_RIGHT) != 0U) && \
    (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_LEFT_ONLY)
#error "右轴控制必须同时选择右轴查询"
#endif

#if (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_LEFT_ONLY) && \
    (APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_LEFT)
#error "LEFT_ONLY配置必须只控制左轴"
#endif

#if (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_RIGHT_ONLY) && \
    (APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_RIGHT)
#error "RIGHT_ONLY配置必须只控制右轴"
#endif

#if (APP_SERVO_QUERY_MODE == APP_SERVO_QUERY_BOTH) && \
    (APP_SERVO_CONTROL_AXIS != APP_SERVO_CONTROL_AXIS_BOTH)
#error "BOTH配置必须同时控制左右轴"
#endif

#if ((APP_SERVO_READONLY_TEST != 0U) && \
     (APP_SERVO_READONLY_TEST != 1U))
#error "APP_SERVO_READONLY_TEST必须为0或1"
#endif


/*
 * 系统任务周期配置，单位：ms。
 */

/*
 * 运动任务周期。
 * 运动规划、速度更新和脉冲输出协调通常每5 ms执行一次。
 */
#define APP_MOTION_PERIOD_MS \
    (5U)

/*
 * 健康监控任务周期。
 * 用于检查关键任务心跳、通信状态和系统故障。
 */
#define APP_HEALTH_PERIOD_MS \
    (100U)
/* 快照发布节拍；健康判定仍使用上面的100ms周期。 */
#define APP_HEALTH_TASK_PERIOD_MS (10U)

/*
 * 按键扫描任务周期。
 */
#define APP_KEY_PERIOD_MS \
    (5U)

/*
 * 状态任务周期。
 * 用于更新系统状态、LED、蜂鸣器等诊断输出。
 */
#define APP_STATUS_PERIOD_MS \
    (50U)

/*
 * LCD刷新任务周期。
 * LCD作为低优先级诊断显示，当前每500 ms刷新一次。
 */
#define APP_LCD_PERIOD_MS \
    (500U)

/*
 * 独立看门狗任务周期。
 * 看门狗任务按照该周期检查关键任务心跳并决定是否刷新IWDG。
 */
#define APP_IWDG_PERIOD_MS \
    (20U)

/* 是否启用遥控器K6短按看门狗复位测试。 */
/* 旧K2开关只兼容配置名称，不恢复旧物理按键功能。 */
#if defined(APP_WDG_TEST_K6_SHORT_ENABLED) && defined(APP_WDG_TEST_K2_SHORT_ENABLED)
#error "K6看门狗开关的新旧宏不能同时定义"
#endif
#ifndef APP_WDG_TEST_K6_SHORT_ENABLED
#ifdef APP_WDG_TEST_K2_SHORT_ENABLED
#define APP_WDG_TEST_K6_SHORT_ENABLED APP_WDG_TEST_K2_SHORT_ENABLED
#else
#define APP_WDG_TEST_K6_SHORT_ENABLED (0U)
#endif
#endif
#ifndef APP_WDG_TEST_K2_SHORT_ENABLED
#define APP_WDG_TEST_K2_SHORT_ENABLED APP_WDG_TEST_K6_SHORT_ENABLED
#endif

#if (APP_WDG_TEST_K6_SHORT_ENABLED != 0U) && \
    (APP_WDG_TEST_K6_SHORT_ENABLED != 1U)
#error "遥控器K6短按看门狗测试开关仅允许配置为0或1"
#endif

/* 看门狗恢复读取、核验及提交零速指令的时限，不含实际减速。 */
#define APP_WDG_RECOVERY_TIMEOUT_MS \
    (3000U)

/* 每1000 rpm减速时间，6250ms对应1600rpm在10秒内降至0。 */
#ifndef APP_WDG_RECOVERY_DECEL_MS
#define APP_WDG_RECOVERY_DECEL_MS (6250U)
#endif

/* 实际停车确认余量，不改变正常调速及K5、失联停车参数。 */
#ifndef APP_WDG_RECOVERY_STOP_MARGIN_MS
#define APP_WDG_RECOVERY_STOP_MARGIN_MS (2000U)
#endif

#if (APP_WDG_RECOVERY_DECEL_MS < 1U) || (APP_WDG_RECOVERY_DECEL_MS > 65535U)
#error "看门狗恢复减速时间必须为1至65535ms"
#endif
#if (APP_WDG_RECOVERY_STOP_MARGIN_MS < 1U) || \
    (APP_WDG_RECOVERY_STOP_MARGIN_MS > 0x3FFFFFFFUL)
#error "看门狗停车确认余量超出Tick安全范围"
#endif

/* 看门狗恢复每轴失能发送总次数及单个失败步骤的重试上限。 */
#define APP_WDG_RECOVERY_RETRY_MAX \
    (3U)

/* 看门狗恢复重试最小间隔，单位：ms。 */
#define APP_WDG_RECOVERY_RETRY_INTERVAL_MS \
    (100U)


/*
 * 运动硬件和伺服参数确认门禁。
 *
 * 这些宏用于表示项目当前是否已经完成对应硬件参数确认。
 * 宏值为1表示软件允许将该项视为已确认。
 * 宏值为0表示相关功能应继续保持禁止。
 *
 * 注意：
 * 这些宏不能代替真实硬件测量、厂家资料核对和伺服参数回读。
 */

/*
 * AM26C31脉冲输出使能极性是否已经确认。
 * 确认后，软件才允许根据PC8/PC9控制脉冲输出门禁。
 */
#define APP_SERVO_OUTPUT_POLARITY_CONFIRMED \
    (1U)

/*
 * PB0/PB1方向控制电平定义是否已经确认。
 * 当前约定为高电平正转、低电平反转。
 */
#define APP_SERVO_DIRECTION_CONFIRMED \
    (1U)

/*
 * 左右伺服Modbus从站地址是否已经确认。
 * 当前左轴地址为1，右轴地址为2。
 */
#define APP_SERVO_ADDRESS_CONFIRMED \
    (1U)

/*
 * 伺服脉冲输入格式是否已经确认。
 * 当前项目使用“脉冲+方向”控制形式。
 */
#define APP_SERVO_PULSE_FORMAT_CONFIRMED \
    (1U)

/*
 * 伺服电子齿轮参数是否已经确认。
 * 该宏为1只表示项目配置已确认，仍需要实际回读核验。
 */
#define APP_SERVO_ELECTRONIC_GEAR_CONFIRMED \
    (1U)


/* 
APP_SERVO_MAINTENANCE_STOP_CONFIRMED=1：允许进入维护模式；
APP_SERVO_SOFT_ENABLE_CONFIRMED=1：允许通过RS485执行模式配置、使能和速度/位置命令。
*/

/*
 * 伺服软件使能功能是否已经完成台架确认。
 *
 * 为0时，软件禁止通过RS485执行伺服使能相关控制。
 * 该项不能仅依据接线或代码配置确认。
 */
#define APP_SERVO_SOFT_ENABLE_CONFIRMED \
    (1U)

/*
 * 维护模式停止方式是否已经完成台架确认。
 *
 * 当前值为1。维护模式仍可作为人工停机/调试隔离开关，
 * 但不再作为正常串口速度和位置命令的必经条件。
 */
#define APP_SERVO_MAINTENANCE_STOP_CONFIRMED \
    (1U)

/*
 * 是否启用上电后一次零点记录流程。
 *
 * 当前项目已完成位置基准确认，串口调试不再强制执行上电设零流程，
 * 因此默认关闭。需要重新建立软件零点时，再通过专用流程显式执行。
 */
#define APP_SERVO_BOOT_ZERO_ENABLED \
    (0U)

/*
 * 左轴方向控制的正转有效电平。
 *
 * 当前值为1，表示左轴方向信号为高电平时代表正转。
 */
#define APP_SERVO_LEFT_FORWARD_LEVEL \
    (1U)

/*
 * 右轴方向控制的正转有效电平。
 *
 * 当前值为1，表示右轴方向信号为高电平时代表正转。
 */
#define APP_SERVO_RIGHT_FORWARD_LEVEL \
    (1U)


/*
 * AIMtor伺服驱动器参数配置。
 *
 * 这些参数用于生成和核对驱动器寄存器配置。
 * 只有经过厂家资料核对和实际回读验证后，才可作为正式控制依据。
 */

/*
 * AIMtor脉冲输入格式参数值。
 * 当前值0表示项目定义的默认脉冲格式编码。
 * 具体编码含义必须与厂家手册中的寄存器定义一致。
 */
#define APP_AIMTOR_PULSE_FORMAT_VALUE \
    (0U)

/*
 * AIMtor电子齿轮分子。
 * 当前配置为131072。
 */
#define APP_AIMTOR_ELECTRONIC_GEAR_NUMERATOR \
    (131072UL)

/*
 * AIMtor电子齿轮分母。
 * 当前配置为1000。
 */
#define APP_AIMTOR_ELECTRONIC_GEAR_DENOMINATOR \
    (1000UL)


/*
 * 判断运动硬件门禁是否满足。
 *
 * 返回值：
 *   1：输出使能极性和方向控制电平均已确认；
 *   0：至少有一项硬件参数未确认，禁止开放运动。
 *
 * 注意：
 * 该函数只检查软件配置门禁，不代表伺服已经在线、
 * 参数已经回读或急停/STO已经验证。
 */
uint8_t app_config_motion_hardware_ready(void);


/*
 * 判断伺服总线配置是否满足。
 *
 * 返回值：
 *   1：伺服地址配置已经确认；
 *   0：伺服地址尚未确认，禁止依赖总线进行正式控制。
 *
 * 注意：
 * 该函数只检查地址确认宏，不代表RS485已经建立通信。
 */
uint8_t app_config_servo_bus_ready(void);


/*
 * 判断伺服脉冲参数是否满足。
 *
 * 返回值：
 *   1：脉冲格式和电子齿轮参数均已确认；
 *   0：至少有一项参数未确认，禁止进入正式运动控制。
 *
 * 注意：
 * 返回1只表示配置门禁通过，仍需通过Modbus读取驱动器实际参数进行核验。
 */
uint8_t app_config_servo_parameters_confirmed(void);


/* 判断指定轴是否参与本次查询，0表示左轴，1表示右轴。 */
uint8_t app_config_servo_axis_selected(uint8_t axis);

/* 返回只读测试总开关状态。 */
uint8_t app_config_servo_readonly_test(void);

/* 返回当前允许实际控制的轴掩码。 */
uint8_t app_config_servo_control_axis(void);

/* 判断指定轴是否允许提交控制命令。 */
uint8_t app_config_servo_axis_controllable(uint8_t axis);

/* 返回当前控制轴掩码、数量和名称。 */
uint8_t app_config_servo_control_axis_mask(void);
uint8_t app_config_servo_control_axis_count(void);
const char *app_config_servo_control_axis_name(void);

/* 浮点参数不能用预处理器比较，在使能和配置寄存器之前拒绝非法值。 */
static inline uint8_t app_config_motion_ramp_valid(void)
{
    return (uint8_t)(
        APP_WHEEL_DIAMETER_M > 0.0f && APP_WHEEL_DIAMETER_M < 1000.0f &&
        APP_GEAR_REDUCTION_RATIO > 0.0f && APP_GEAR_REDUCTION_RATIO < 1000000.0f &&
        (double)APP_DESIGN_ACCELERATION_MPS2 > 0.0 && (double)APP_DESIGN_ACCELERATION_MPS2 < 1000000.0 &&
        (double)APP_DESIGN_DECELERATION_MPS2 > 0.0 && (double)APP_DESIGN_DECELERATION_MPS2 < 1000000.0 &&
        APP_HMI_NORMAL_STOP_DECEL_MPS2 > 0.0f && APP_HMI_NORMAL_STOP_DECEL_MPS2 < 65.535f &&
        APP_HMI_LINK_STOP_DECEL_MPS2 > 0.0f && APP_HMI_LINK_STOP_DECEL_MPS2 < 65.535f &&
        APP_HMI_EMERGENCY_STOP_DECEL_MPS2 > 0.0f && APP_HMI_EMERGENCY_STOP_DECEL_MPS2 < 65.535f &&
        APP_TASK_STOP_DECEL_MPS2 > 0.0f && APP_TASK_STOP_DECEL_MPS2 < 65.535f &&
        (double)APP_SERVO_SAFETY_STOP_DECEL_MPS2 > 0.0 && (double)APP_SERVO_SAFETY_STOP_DECEL_MPS2 < 1000000.0 &&
        (double)APP_CAN_QUICK_STOP_DECEL_MPS2 > 0.0 && (double)APP_CAN_QUICK_STOP_DECEL_MPS2 < 65.535 &&
        (double)APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 > 0.0 && (double)APP_REMOTE_SERVO_LINK_STOP_DECEL_MPS2 < 1000000.0 &&
        (double)APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 > 0.0 && (double)APP_REMOTE_SERVO_K5_STOP_DECEL_MPS2 < 1000000.0 &&
        (double)APP_CAN_NORMAL_STOP_DECEL_MPS2 > 0.0 && (double)APP_CAN_NORMAL_STOP_DECEL_MPS2 < 1000000.0 &&
        (double)APP_CAN_LINK_STOP_DECEL_MPS2 > 0.0 && (double)APP_CAN_LINK_STOP_DECEL_MPS2 < 1000000.0 &&
        (double)APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS >= 1.0 &&
        (double)APP_SERVO_SPEED_ACCELERATION_RAMP_RAW_MS <= 65535.0 &&
        (double)APP_SERVO_SPEED_DECELERATION_RAMP_RAW_MS >= 1.0 &&
        (double)APP_SERVO_SPEED_DECELERATION_RAMP_RAW_MS <= 65535.0);
}
#endif /* APP_CONFIG_H */
