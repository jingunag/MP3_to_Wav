/*
 * CsdOut.h
 *
 *  Created on: 2022年4月22日
 *      Author: Y5298
 */

#ifndef CSDOUT_H_
#define CSDOUT_H_

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ctype.h>
#include <process.h>
#include <pthread.h>
#include <string.h>
#include <gulliver.h>
#include <sys/mman.h>
#include <sys/pps.h>
#include <sys/select.h>
#include <fcntl.h>



extern "C" {

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"


#include "dll_utils_i.h"
#include "csd.h"
#include "csd_as.h"
#include "csd_as_fmt.h"
#include "csd_ac.h"
#include "csd_as_ioctl.h"
#include "csd_dev_ioctl.h"
#include "csd_dev_oem.h"
#include "csd_alm_ioctl.h"
#include "csd_apm_ioctl.h"
}

#include "FYTime.h"
#include "Systemdef.h"
#include "SystemMidController.h"

#define NUM_CHANNELS_MONO                    (1)
#define NUM_CHANNELS_STEREO                  (2)
#define NUM_CHANNELS_4CH_STEREO              (4)
#define MAX_NUM_CHANNELS                     (8)
#define MAX_NUM_CHANNELS_V2                  (32)

#define CHANNEL_STEREO_NONE                  (0)
#define CHANNEL_STEREO_LEFT                  (1)
#define CHANNEL_STEREO_RIGTH                 (2)

#define INSIDE_SPEAKER                 		 (1) // 车内通道
#define OUTSIDE_SPEAKER                		 (2) // 车外通道
#define POWER_RADAR                		     (3) // 外置功放雷达通道
#define REF_CHANNEL                		     (4) // ref参考音通道

// InSide Speaker
#define CHANNEL_STEREO_FL                  (1)  // 1000
#define CHANNEL_STEREO_FR                  (2)  // 0100
#define CHANNEL_STEREO_RL                  (4)  // 0010
#define CHANNEL_STEREO_RR                  (8)  // 0001
#define CHANNEL_STEREO_FL_RL               (5)  // 1010
#define CHANNEL_STEREO_FR_RR               (10) // 0101
#define CHANNEL_STEREO_FL_FR               (3)  // 1100
#define CHANNEL_STEREO_RL_RR               (12) // 0011
#define CHANNEL_STEREO_FLFR_RLRR           (15) // 1111

#define CHANNEL_STEREO_FR_RL               (6)  // 0110
#define CHANNEL_STEREO_FL_RR               (9)  // 1001
#define CHANNEL_STEREO_FLFR_RR             (11)  // 1101
#define CHANNEL_STEREO_FLFR_RL             (7)   // 1110
#define CHANNEL_STEREO_FL_RLRR             (13)  // 1011
#define CHANNEL_STEREO_FR_RLRR             (14)  // 0111


// OutSide Speaker
#define OUTSIDE_SPEAKER_ONE              	(1) // 1000
#define OUTSIDE_SPEAKER_TWO              	(3) // 1100

#define DEV_SAMPLE_RATE                      (48000)

#define CSD_BUF_DONE_EVENT                   (0x00000001)
#define CSD_BUF_READY_EVENT                  (0x00000002)
#define CSD_EOS_EVENT                        (0x00000004)
#define CSD_SWITCH_OFF_EVENT                 (0x00000008)
#define CSD_SWITCH_OFF_DONE_EVENT            (0x00000010)

#define BUFFER_POOL_COUNT                    (2)
#define BUFFER_POOL_BUF_SIZE                 (4096)

typedef struct buffer_pool {
	uint8_t *pVa; /* Virtual address */
	uint8_t *pPa; /* Physical address */
	uint8_t *pHeap;
	uint8_t bAllocated; /* FALSE = 0, TRUE = 1 */
	uint8_t bUsed; /* FALSE = 0, TRUE = 1 */
	uint32_t nCsdBufSize; /* Buffer size reported by CSD callback */
} buffer_pool_t;

typedef struct audio_src {
	FILE *pFile;

	int32_t sampleChannels;
	int32_t sampleRate;
	int32_t byteRate;
	int32_t sampleBlockAlign;
	int32_t sampleBits;

	int32_t sampleWordBits;

	int32_t sampleSign;
	int32_t sampleInterleave;
	int32_t sampleEndianness;
	int32_t sampleMode;

	uint32_t csdFormat;

	uint32_t bufByteRead;
	uint32_t bufByteUsed;

	uint32_t dataPosition;
	uint32_t dataTotal;
	uint32_t dataReaded;

	uint64_t duration;
} audio_src_t;

class SystemMidController;

class CsdOut {
public:
	CsdOut();
	~CsdOut();

public:
	///////////////////////////////////////////////////////////////////////////////
	/**
	 *  @brief PlayFile 播放函数
	 *
	 *   1.播放指定的wav音频文件
	 *
	 *   @param const CHAR * strFileName  音频文件名
	 *
	 *   @param S_AUDIO_INFO * pAudioInfo   播放信息
	 *
	 *   @return 返回值，正确则返回AUDIO_OUTPUT_OK，
	 */
	///////////////////////////////////////////////////////////////////////////////
	INT32 PlayFile(const CHAR * strFileName, S_AUDIO_INFO * pAudioInfo);
	INT32 PlayFile_PCM(const CHAR * strFileName, S_AUDIO_INFO * pAudioInfo);
	INT32 PlayFile_MP3(const CHAR * strFileName, S_AUDIO_INFO * pAudioInfo);
	///////////////////////////////////////////////////////////////////////////////
	/** @brief 设置音量
	 *
	 *   动态设置播放音量
	 *
	 *    @param  需要设置的音量值
	 *
	 */
	///////////////////////////////////////////////////////////////////////////////
	void SetVolume(const int level);
	///////////////////////////////////////////////////////////////////////////////
	/** @brief 获取音量
	 *
	 *   获取播放音量
	 *
	 */
	///////////////////////////////////////////////////////////////////////////////
	INT32 GetVolume();

	///////////////////////////////////////////////////////////////////////////////
	/** @brief 设置Mute状态
	 *
	 *   动态设置Mute状态
	 *
	 *    @param  需要设置Mute状态
	 *
	 */
	///////////////////////////////////////////////////////////////////////////////
	void SetMuteState(const uint32_t mutestate);

	///////////////////////////////////////////////////////////////////////////////
	/** @brief 设置控制器
	 *
	 *   设置控制器实例
	 *
	 *    @param  控制器实例
	 *
	 */
	///////////////////////////////////////////////////////////////////////////////
	void SetPPSControl(SystemMidController *pCtrl);

	void SetDevId(int dev_id);

	void SetAcOpenMask(uint32_t open_mask);

	void SetAsOpenMask(uint32_t open_mask);

	int32_t AudioAsMute(uint32_t mute);

private:
	///////////////////////////////////////////////////////////////////////////////
	/**
	 *  @brief 校验WAV文件头
	 *
	 *   1.检查文件头标志，是否是符合要求的文件
	 *
	 *  @param   FILE *	fp		文件指针
	 *
	 *  @param bool *pBigEndian  指针
	 *
	 *  @return 正确返回0，否则返回-1
	 */
	///////////////////////////////////////////////////////////////////////////////
	int checkHdr(FILE *fp, bool *pBigEndian);
	/**
	 *  @brief 查找文件中包含的字符串标识
	 *
	 *  @param FILE * fp   文件指针
	 *
	 *  @param const char * tag  要查找的字符串标识
	 *
	 *  @return 若没有查到，则返回0；否则，返回该字符串所标识的数据长度
	 */
	///////////////////////////////////////////////////////////////////////////////
	int32_t findTag(FILE *fp, const char *tag, const bool bigEndian);

	int32_t initFile(const CHAR *strFileName, audio_src_t *pAudioSrc, INT32 channel, int alarmId);
	int32_t initFile_PCM(const CHAR *strFileName, audio_src_t *pAudioSrc, INT32 channel, int alarmId);
	int AudioMp3ToWAV(const CHAR *inFileName, const CHAR *outFileName);

	int32_t initCsd(const int audioZone, const int channel, int alarmId);

	uint32_t openApm(void);

	uint32_t openDevice(void);

	uint32_t openAudioStreamV2(uint32_t buf_mem_type, uint32_t bit_width,
			uint32_t format_type, uint32_t open_mask);

	uint32_t openAudioContext(uint32_t dev_id, uint32_t sample_rate);

	uint32_t openAudioContextV2(uint32_t dev_id, uint32_t sample_rate,
			uint32_t sample_width, uint32_t open_mask, uint32_t ac_category);

	int32_t streamAttach(uint32_t ac_handle, uint32_t as_handle);

	int32_t setupCsdSession(audio_src_t *pAudioSrc, int audio_channel, int alarmId);

	int32_t streamCsdSession(audio_src_t *pAudioSrc, int repeat_count, int min_count, int interval_time, int audio_channel, int alarm_id);

	uint32_t processStream(uint8_t *pVa, audio_src_t *pAudioSrc);
	uint32_t processStream_7012A(uint8_t *pVa, audio_src_t *pAudioSrc, FILE* pfile,int channel);


	int32_t teardownCsdSession(int audio_channel);

	int32_t enableContext(uint32_t ac_handle);

	int32_t enableDevice(uint32_t dev_id, uint32_t sample_rate,
			uint32_t sample_width, uint32_t dev_handle);

	int32_t setDevVolumeMaster(uint32_t g_dev_handle, uint32_t g_dev_id,
			uint32_t dev_volume);

	int32_t setDevMute(uint32_t g_dev_handle, uint32_t g_dev_id,
			uint32_t dev_mute);

	uint32_t setDevLimiterModule(uint32_t dev_id, bool_t g_dev_limiter_flag);

	int32_t setAsVolumeMaster(uint32_t as_handle, uint32_t vol);

	int32_t setAsMute(uint32_t as_handle, uint32_t mute);
	int32_t setDevMute(uint32_t dev_handle, uint32_t dev_id, csd_pp_vol_mute_t *mute);
	int32_t setStreamFormat(uint32_t as_handle);

	int32_t setStreamFormat2(uint32_t as_handle, uint32_t sample_rate,
			uint16_t channels, uint16_t bit_per_sample, uint16_t sign_flag,
			uint16_t interleave_flag);

	int32_t setStreamFormatPcmMultichannel(uint32_t as_handle,
			uint32_t sample_rate, uint16_t num_channels,
			uint16_t bits_per_sample, uint16_t sign_flag,
			uint16_t interleave_flag, uint8_t *channel_mapping);

	int32_t setStreamFormatPcmMultichannelV2(uint32_t as_handle,
			uint32_t sample_rate, uint16_t num_channels,
			uint16_t bits_per_sample, uint16_t sign_flag,
			uint16_t interleave_flag, uint16_t sample_word_size,
			uint16_t endianness, uint16_t mode, uint8_t *channel_mapping);

	int32_t setStreamContextMultichannel(uint32_t as_handle, uint32_t ac_handle,
			uint16_t num_channels, uint8_t *channel_mapping);

	int32_t setStartSession(uint32_t as_handle);

	int32_t setStopSession(uint32_t as_handle);

	int32_t dinitCsd();

	int32_t closeApm(uint32_t apm_handle);

	int32_t apmRegisterClient(uint32_t apm_handle, csd_apm_cb_fn cb,
			void *data);

	int32_t apmDeregisterClient(uint32_t apm_handle);

	int32_t apmRequestPermission(uint32_t apm_handle);

	int32_t apmReleasePermission(uint32_t apm_handle);

	int32_t disableContext(uint32_t ac_handle);

	int32_t streamDetach(uint32_t ac_handle, uint32_t as_handle);

	int32_t disableDevice(uint32_t dev_id, uint32_t dev_handle);

	int32_t closeContext(uint32_t ac_handle);

	int32_t closeStream(uint32_t as_handle);

	int32_t closeDevice(uint32_t dev_handle);

	int32_t registerCallBack(uint32_t as_handle, void *cb, void *cb_data);

	static int32_t allocateCsdBufferPool(uint32_t as_handle,
			buffer_pool_t *buffer_pool, uint32_t buffer_count,
			uint32_t buffer_size);

	static int32_t freeCsdBufferPool(uint32_t as_handle, buffer_pool_t *buffer_pool,
			uint32_t buffer_count, uint32_t buffer_size);

	static int32_t reserveCsdBufferFromPool(buffer_pool_t *buffer,
			uint32_t buffer_count, uint32_t buffer_free_count, uint32_t *index);

	static int32_t releaseCsdBufferFromPool(buffer_pool_t *buffer,
			uint32_t buffer_count, uint32_t buffer_free_count, uint8_t *pBuf,
			uint32_t *token, uint32_t buf_size);

	static void csdUserCallBack(uint32_t evntId, void *pPayload,
			uint32_t payloadSize, void *pClientData);

private:
	void lock(void) {
		m_Lock.Lock();
	}
	void unLock(void) {
		m_Lock.Unlock();
	}

public:
	bool m_bStop;
	bool m_bExit;
	bool m_bPlaying;
	int m_iVolLevel;
	uint32_t m_iMuteState;

	SystemMidController* m_pPPSCtrl;

private:
	MutexLock m_Lock;
	uint32_t mychannel;
	buffer_pool_t m_buffer_pool[BUFFER_POOL_COUNT];
	uint32_t m_buffer_pool_count;
	uint32_t m_buffer_pool_buf_size;
	uint32_t m_buffer_pool_free_count;

	pthread_condattr_t m_csd_attr;
	pthread_mutex_t m_csd_mutex;
	pthread_cond_t m_csd_cond;
	//pthread_cond_t m_csd_cb_cond;

	uint32_t m_csd_event_type;
	uint32_t m_apm_cb_ready; /* context is ready for apm callback to run */

	uint32_t m_dev_id;
	int32_t m_dev_sample_bits;
	int32_t m_dev_channels;

	/**
	 * 0: Stereo, default value
	 * 1: Left
	 * 2: Right
	 */
	int32_t m_as_channel;

	uint32_t m_as_volume;
	uint32_t m_as_outvolume;
	uint32_t m_dev_volume;

	uint32_t m_as_open_mask;
	uint32_t m_ac_open_mask;

	uint32_t m_apm_handle;
	uint32_t m_dev_handle;
	uint32_t m_ac_handle;
	uint32_t m_as_handle;
};

#endif /* CSDOUT_H_ */
