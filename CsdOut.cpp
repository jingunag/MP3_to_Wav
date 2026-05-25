/*
 * CsdOut.cpp
 *
 *  Created on: 2022年4月22日
 *      Author: Y5298
 */

#include "CsdOut.h"
#include "SystemData.h"

static const char *kRiffId = "RIFF";
static const char *kRifxId = "RIFX";
static const char *kWaveId = "WAVE";
static const char *kFmtId = "fmt ";
static const char *kDataId = "data";

#define MP3TOWAV_PATH    "/fs/data/AudioService.wav"

static long long sDecodeTime = 0;
long long getTimestamp()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);

    long long timestamp = tv.tv_sec;
    timestamp = timestamp * 1000 + tv.tv_usec / 1000;
    timestamp = timestamp + 1000 * 60 * 60 * 2;

    // LOGINF("getTimestamp: %lld", timestamp);
    return timestamp;
}

//static inline size_t memscpy(void *dst, size_t dst_size, const void *src, size_t src_size)
static inline size_t memscpy(void *dst, const void *src, size_t src_size)
{
    //size_t copy_size = (dst_size <= src_size) ? dst_size : src_size;
	size_t copy_size = src_size;
    memcpy(dst, src, copy_size);
    return copy_size;
}


CsdOut::CsdOut() {
	m_bStop = false;
	m_bExit = false;
	m_bPlaying = false;
	mychannel = 0;
	m_iVolLevel = 30;
	m_iMuteState = 0;

	m_dev_id = 0;
	m_dev_sample_bits = CSD_DEV_BPS_24;
	m_dev_channels = 2;
	m_dev_volume = 24;

	m_as_channel = 0;
	m_as_volume = 75;
	m_as_outvolume = 75;

	m_apm_handle = 0;
	m_dev_handle = 0;
	m_ac_handle = 0;
	m_as_handle = 0;

	for (int i = 0; i < BUFFER_POOL_COUNT; i++) {
		m_buffer_pool[i]= {0};
	}
	m_buffer_pool_count = BUFFER_POOL_COUNT;
	m_buffer_pool_buf_size = BUFFER_POOL_BUF_SIZE;
	m_buffer_pool_free_count = BUFFER_POOL_COUNT;

	m_csd_event_type = 0;
	m_apm_cb_ready = 0;

	m_ac_open_mask = 0;
	m_as_open_mask = 0;

	m_pPPSCtrl = NULL;


	pthread_mutex_init(&m_csd_mutex, NULL);
	pthread_condattr_init(&m_csd_attr);
	pthread_condattr_setclock(&m_csd_attr, CLOCK_MONOTONIC);
	pthread_cond_init(&m_csd_cond, &m_csd_attr);
}

CsdOut::~CsdOut() {
	pthread_mutex_destroy(&m_csd_mutex);
	pthread_cond_destroy(&m_csd_cond);
}

void CsdOut::SetPPSControl(SystemMidController *pCtrl) {
	if (NULL != pCtrl) {
		m_pPPSCtrl = pCtrl;
	} else {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_32);
	}
}

void CsdOut::SetDevId(int dev_id) {
	m_dev_channels = 2;

	switch (dev_id) {
	case CSD_OEM_SPKR_PHONE_SPKR_STEREO: {
		m_dev_id = CSD_OEM_SPKR_PHONE_SPKR_STEREO;
		break;
	}
	case CSD_OEM_2CH_ENT_RX: {
		m_dev_id = CSD_OEM_2CH_ENT_RX;
		break;
	}
	case CSD_OEM_QUAT_TDM_MULTI_LANE_SD1_8CH_SPKR: {
		m_dev_id = CSD_OEM_QUAT_TDM_MULTI_LANE_SD1_8CH_SPKR;
		//m_dev_channels = 8;
		break;
	}
	case CSD_OEM_QUAT_TDM_MULTI_LANE_SD3_8CH_SPKR: {
		m_dev_id = CSD_OEM_QUAT_TDM_MULTI_LANE_SD3_8CH_SPKR;
		//m_dev_channels = 8;
		break;
	}
	case CSD_OEM_QUAT_TDM_RX:{
		m_dev_id = CSD_OEM_QUAT_TDM_RX;
		break;
	}
	case CSD_OEM_SEC_TDM_RX_CHIME_4CH:
	{
		m_dev_id = CSD_OEM_SEC_TDM_RX_CHIME_4CH;
		m_dev_channels = 4;
		break;
	}
	case CSD_OEM_SEC_TDM_RX_AVAS_2CH:
	{
		m_dev_id = CSD_OEM_SEC_TDM_RX_AVAS_2CH;
		m_dev_channels = 2;
		break;
	}
	case CSD_OEM_QUAT_TDM_RX_CHIME_1CH:
	{
		m_dev_id = CSD_OEM_QUAT_TDM_RX_CHIME_1CH;
        m_dev_channels = 1;
        break;
	}
	case CSD_OEM_SEC_TDM_RX_TTS_2CH:
	{
		m_dev_id = CSD_OEM_SEC_TDM_RX_TTS_2CH;
		m_dev_channels = 2;
		break;
	}
	case CSD_OEM_PRI_TDM_RX_REF_4CH:
	{
		m_dev_id = CSD_OEM_PRI_TDM_RX_REF_4CH;
		m_dev_channels = 4;
		break;
	}
	default: {
		LOGERR("Device ID <%d> is not supported", dev_id);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_40);
		break;
	}
	}
	LOGINF("SetDevId : %d, SetDevChannels: %d", m_dev_id, m_dev_channels);
}

void CsdOut::SetAcOpenMask(uint32_t open_mask) {
	this->m_ac_open_mask = open_mask;
}

void CsdOut::SetAsOpenMask(uint32_t open_mask) {
	this->m_as_open_mask = open_mask;
}

int CsdOut::checkHdr(FILE *fp, bool *pBigEndian) {
	RiffHdr riffHdr = { "", 0, "" };

	size_t count = 1;
	size_t size = 0;
	size = fread((unsigned char *) &riffHdr, sizeof(RiffHdr), count, fp);
	if (size < count) {
		return -1;
	}

	if (!strncmp(riffHdr.Riff, kRiffId, strlen(kRiffId))) {
		*pBigEndian = false;
	} else if (!strncmp(riffHdr.Riff, kRifxId, strlen(kRifxId))) {
		*pBigEndian = true;
	} else {
		return -1;
	}

	if (strncmp(riffHdr.Wave, kWaveId, strlen(kWaveId))) {
		return -1;
	}

	return 0;
}

int32_t CsdOut::findTag(FILE *fp, const char *tag, const bool bigEndian) {
	int32_t retVal = 0;
	RiffTag tagBfr = { "", 0 };
	size_t count = 1;
	size_t size = 0;

	// Keep reading until we find the tag or hit the EOF.
	do {
		size = fread((unsigned char *) &tagBfr, sizeof(tagBfr), count, fp);
		if (size < count) {
			break;
		}

		tagBfr.length =
				bigEndian ?
						ENDIAN_BE32(tagBfr.length) : ENDIAN_LE32(tagBfr.length);
		// If this is our tag, set the length and break.
		if (strncmp(tag, tagBfr.tag, sizeof tagBfr.tag) == 0) {
			retVal = tagBfr.length;
			break;
		}

		// Skip ahead the specified number of bytes in the stream
		if (fseek(fp, tagBfr.length, SEEK_CUR) != 0) {
			retVal = 0;
			break;
		}
	} while (true);

	return retVal;
}

INT32 CsdOut::PlayFile(const CHAR *strFileName, S_AUDIO_INFO *pAudioInfo) {
	this->m_bPlaying = true;

	INT32 iReturn = AUDIO_OUTPUT_OK;
	INT32 rc = 0;

	if (NULL == strFileName) {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_33);
		iReturn = AUDIO_OUTPUT_PARAMETER_ERROR;
		return iReturn;
	}

	if (NULL == pAudioInfo) {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_34);
		iReturn = AUDIO_OUTPUT_PARAMETER_ERROR;
		return iReturn;
	}

	INT32 iAudioZone = 0;
	//INT32 iVolumeLevel = 0;
	INT32 iChannel = 1;
	INT32 iRepeatCount = 0;
	INT32 iMinCount = 0;
	INT32 iIntervalTime = 0;
	INT32 iAlarmid = 0;

	audio_src_t audioSrc;
	memset((void *) &audioSrc, 0, sizeof(audio_src_t));

	iAudioZone = pAudioInfo->audio_zone;
	//iVolumeLevel = pAudioInfo->volume_level;
	iChannel = pAudioInfo->audio_channel;
	this->mychannel = pAudioInfo->audio_channel;
	iRepeatCount = pAudioInfo->repeat_count;
	iIntervalTime = pAudioInfo->interval_time;
	iMinCount = pAudioInfo->min_count;
	iAlarmid = pAudioInfo->alarm_id;

	LOGINF("(%d)Play File Start[%llums]...", iChannel,getTickms());
	LOGINF("(%d)&&&---Play file:%s\n", iChannel,strFileName);

	rc = this->initFile(strFileName, &audioSrc,iChannel, iAlarmid);
	if (0 != rc)
	{
		LOGERR("file(%s) initialize failed #1\r\n", strFileName);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_29);
		iReturn = AUDIO_OUTPUT_FILE_CAN_NOT_OPEN;
		return iReturn;
	}

	//LOGINF("(%d)iAudioZone: %d, iAlarmid: %d", iChannel, iAudioZone, iAlarmid);

	if(REF_CHANNEL == iChannel)  // 4 channel 用于写入参考音
	{
		rc = this->initCsd(15, REF_CHANNEL, iAlarmid);
		if (CSD_EOK != rc) {
			LOGERR("(%d)initCsd failed\r\n", iChannel);
			iReturn = AUDIO_OUTPUT_INIT_FAILED;
			goto end;
		}
	}
	else
	{
		rc = this->initCsd(iAudioZone, iChannel, iAlarmid);
		if (CSD_EOK != rc) {
			LOGERR("(%d)initCsd failed\r\n", iChannel);
			iReturn = AUDIO_OUTPUT_INIT_FAILED;
			goto end;
		}
	}

	rc = this->setupCsdSession(&audioSrc, iChannel, iAlarmid);
	if (CSD_EOK != rc) {
		LOGERR("(%d)setupCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_SETUP_FAILED;
		goto end;
	}

	rc = this->streamCsdSession(&audioSrc, iRepeatCount, iMinCount,
			iIntervalTime, iChannel, iAlarmid);
	if (CSD_EOK != rc) {
		LOGERR("(%d)streamCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_STREAM_FAILED;
		goto end;
	}

	rc = this->teardownCsdSession(iChannel);
	if (CSD_EOK != rc) {
		LOGERR("(%d)teardownCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_TEARDOWN_FAILED;
		goto end;
	}

	rc = this->dinitCsd();
	if (CSD_EOK != rc) {
		LOGERR("(%d)dinitCsd failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_DINIT_FAILED;
		goto end;
	}

end:
	if (NULL != audioSrc.pFile) {
		fclose(audioSrc.pFile);
	}

	if (NULL != m_pPPSCtrl)
	{
		LOGINF("(%d)WAV m_pPPSCtrl->Publish_AudioZone!!!",iChannel);
		m_pPPSCtrl->Publish_AudioZone(pAudioInfo->audio_zone,
				pAudioInfo->audio_channel, pAudioInfo->alarm_id,
				(this->m_bExit || this->m_bStop) ? 3/*中断*/: 2/*结束*/);
	}
	else
	{
		LOGERR("m_pPPSCtrl is NULL");
	}

	LOGINF("(%d)Play File End[%llums]", iChannel, getTickms());

	this->m_bPlaying = false;

	return iReturn;
}

INT32 CsdOut::PlayFile_PCM(const CHAR *strFileName, S_AUDIO_INFO *pAudioInfo) {
	this->m_bPlaying = true;

	INT32 iReturn = AUDIO_OUTPUT_OK;
	INT32 rc = 0;

	if (NULL == strFileName) {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_33);
		iReturn = AUDIO_OUTPUT_PARAMETER_ERROR;
		return iReturn;
	}

	if (NULL == pAudioInfo) {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_34);
		iReturn = AUDIO_OUTPUT_PARAMETER_ERROR;
		return iReturn;
	}

	INT32 iAudioZone = 0;
	//INT32 iVolumeLevel = 0;
	INT32 iChannel = 1;
	INT32 iRepeatCount = 0;
	INT32 iMinCount = 0;
	INT32 iIntervalTime = 0;
	INT32 iAlarmid = 0;

	audio_src_t audioSrc;
	memset((void *) &audioSrc, 0, sizeof(audio_src_t));

	iAudioZone = pAudioInfo->audio_zone;
	//iVolumeLevel = pAudioInfo->volume_level;
	iChannel = pAudioInfo->audio_channel;
	this->mychannel = pAudioInfo->audio_channel;
	iRepeatCount = pAudioInfo->repeat_count;
	iIntervalTime = pAudioInfo->interval_time;
	iMinCount = pAudioInfo->min_count;
	iAlarmid = pAudioInfo->alarm_id;

	LOGINF("(%d)Play File Start[%llums]...", iChannel,getTickms());
	LOGINF("(%d)&&&---Play file:%s\n", iChannel,strFileName);

	rc = this->initFile_PCM(strFileName, &audioSrc,iChannel, iAlarmid);
	if (0 != rc)
	{
		LOGERR("file(%s) initialize failed #1\r\n", strFileName);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_29);
		iReturn = AUDIO_OUTPUT_FILE_CAN_NOT_OPEN;
		return iReturn;
	}

	LOGINF("(%d)iAudioZone: %d, iAlarmid: %d", iChannel, iAudioZone, iAlarmid);
	// Voice commands from outside the car
	// Set default parameters channel and sound range
	// 车外语音指令
	// 设置默认参数通道和音区
	if(REF_CHANNEL == iChannel)
	{
		rc = this->initCsd(15, REF_CHANNEL, iAlarmid);
		if (CSD_EOK != rc) {
			LOGERR("(%d)initCsd failed\r\n", iChannel);
			iReturn = AUDIO_OUTPUT_INIT_FAILED;
			goto end;
		}
	}
	else
	{
		rc = this->initCsd(OUTSIDE_SPEAKER_TWO, OUTSIDE_SPEAKER, iAlarmid);
		if (CSD_EOK != rc) {
			LOGERR("(%d)initCsd failed\r\n", iChannel);
			iReturn = AUDIO_OUTPUT_INIT_FAILED;
			goto end;
		}
	}

	rc = this->setupCsdSession(&audioSrc, iChannel, iAlarmid);
	if (CSD_EOK != rc) {
		LOGERR("(%d)setupCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_SETUP_FAILED;
		goto end;
	}

	rc = this->streamCsdSession(&audioSrc, iRepeatCount, iMinCount,
			iIntervalTime, iChannel, iAlarmid);
	if (CSD_EOK != rc) {
		LOGERR("(%d)streamCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_STREAM_FAILED;
		goto end;
	}

	rc = this->teardownCsdSession(iChannel);
	if (CSD_EOK != rc) {
		LOGERR("(%d)teardownCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_TEARDOWN_FAILED;
		goto end;
	}

	rc = this->dinitCsd();
	if (CSD_EOK != rc) {
		LOGERR("dinitCsd failed\r\n");
		iReturn = AUDIO_OUTPUT_DINIT_FAILED;
		goto end;
	}

end:
	if (NULL != audioSrc.pFile) {
		fclose(audioSrc.pFile);
	}

	if (NULL != m_pPPSCtrl) {
		LOGDBG("PCM m_pPPSCtrl->Publish_AudioZone!!!");
		m_pPPSCtrl->Publish_AudioZone(pAudioInfo->audio_zone,
				pAudioInfo->audio_channel, pAudioInfo->alarm_id,
				(this->m_bExit || this->m_bStop) ? 3/*中断*/: 2/*结束*/);
		if(pAudioInfo->audio_channel != 4) // 参考音不发布PPS
		{
			m_pPPSCtrl->Publish_PCMStatus((this->m_bExit || this->m_bStop) ? 3/*中断*/: 2/*结束*/);
		}
	}

	LOGINF("(%d)Play File End[%llums]", iChannel,getTickms());

	this->m_bPlaying = false;

	return iReturn;
}

INT32 CsdOut::PlayFile_MP3(const CHAR *strFileName, S_AUDIO_INFO *pAudioInfo) {
	this->m_bPlaying = true;

	INT32 iReturn = AUDIO_OUTPUT_OK;
	INT32 rc = 0;

	if (NULL == strFileName) {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_33);
		iReturn = AUDIO_OUTPUT_PARAMETER_ERROR;
		return iReturn;
	}

	if (NULL == pAudioInfo) {
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_34);
		iReturn = AUDIO_OUTPUT_PARAMETER_ERROR;
		return iReturn;
	}

	INT32 iAudioZone = 0;
	//INT32 iVolumeLevel = 0;
	INT32 iChannel = 1;
	INT32 iRepeatCount = 0;
	INT32 iMinCount = 0;
	INT32 iIntervalTime = 0;
	INT32 iAlarmid = 0;

	audio_src_t audioSrc;
	memset((void *) &audioSrc, 0, sizeof(audio_src_t));

	iAudioZone = pAudioInfo->audio_zone;
	//iVolumeLevel = pAudioInfo->volume_level;
	iChannel = pAudioInfo->audio_channel;
	this->mychannel = pAudioInfo->audio_channel;
	iRepeatCount = pAudioInfo->repeat_count;
	iIntervalTime = pAudioInfo->interval_time;
	iMinCount = pAudioInfo->min_count;
	iAlarmid = pAudioInfo->alarm_id;

	LOGINF("(%d)Play File Start[%llums]...", iChannel,getTickms());

	if(REF_CHANNEL != iChannel)
	{
		AudioMp3ToWAV(strFileName, MP3TOWAV_PATH);
	}
	else if(REF_CHANNEL == iChannel)
	{
		//  等待1s时间, AudioMp3ToWAV
		sleep(1);
	}

	if(REF_CHANNEL != iChannel)  // 4 channel 用于写入参考音
	{
		LOGINF("&&& Play file:%s\n", strFileName);
		LOGINF("&&& Mp3 To WAV:%s\n", MP3TOWAV_PATH);
	}

	rc = this->initFile(MP3TOWAV_PATH, &audioSrc,iChannel, iAlarmid);
	if (0 != rc)
	{
		LOGERR("file(%s) initialize failed #1\r\n", MP3TOWAV_PATH);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_29);
		iReturn = AUDIO_OUTPUT_FILE_CAN_NOT_OPEN;
		return iReturn;
	}

	LOGINF("(%d)iAudioZone: %d, iAlarmid: %d", iChannel, iAudioZone, iAlarmid);
	// Voice commands from outside the car
	// Set default parameters channel and sound range
	if(REF_CHANNEL == iChannel)
	{
		rc = this->initCsd(15, REF_CHANNEL, iAlarmid);
		if (CSD_EOK != rc) {
			LOGERR("(%d)initCsd failed\r\n", iChannel);
			iReturn = AUDIO_OUTPUT_INIT_FAILED;
			goto end;
		}
	}
	else
	{
		rc = this->initCsd(iAudioZone, iChannel, iAlarmid);
		if (CSD_EOK != rc) {
			LOGERR("(%d)initCsd failed\r\n", iChannel);
			iReturn = AUDIO_OUTPUT_INIT_FAILED;
			goto end;
		}
	}

	rc = this->setupCsdSession(&audioSrc, iChannel, iAlarmid);
	if (CSD_EOK != rc) {
		LOGERR("(%d)setupCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_SETUP_FAILED;
		goto end;
	}

	rc = this->streamCsdSession(&audioSrc, iRepeatCount, iMinCount,
			iIntervalTime, iChannel, iAlarmid);
	if (CSD_EOK != rc) {
		LOGERR("(%d)streamCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_STREAM_FAILED;
		goto end;
	}

	rc = this->teardownCsdSession(iChannel);
	if (CSD_EOK != rc) {
		LOGERR("(%d)teardownCsdSession failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_TEARDOWN_FAILED;
		goto end;
	}

	rc = this->dinitCsd();
	if (CSD_EOK != rc) {
		LOGERR("(%d)dinitCsd failed\r\n", iChannel);
		iReturn = AUDIO_OUTPUT_DINIT_FAILED;
		goto end;
	}

end:
	if (NULL != audioSrc.pFile) {
		fclose(audioSrc.pFile);
	}

	if (NULL != m_pPPSCtrl) {
		LOGDBG("MP3 m_pPPSCtrl->Publish_AudioZone!!!");
		m_pPPSCtrl->Publish_AudioZone(pAudioInfo->audio_zone,
				pAudioInfo->audio_channel, pAudioInfo->alarm_id,
				(this->m_bExit || this->m_bStop) ? 3/*中断*/: 2/*结束*/);
	}

	LOGINF("(%d)Play File End[%llums]", iChannel,getTickms());

	this->m_bPlaying = false;

	return iReturn;
}

void CsdOut::SetVolume(const int level) {
	lock();
	m_iVolLevel = level;
	if (0 <= level && level < 10)
	{
		m_as_volume = level * 10;
	}
	else
	{
		m_as_volume = 99;
	}
	LOGINF("setAsVolumeMaster(volumeLevel=%d,m_as_volume=%d) \r\n", level, m_as_volume);

//	if (0 != this->m_as_handle)
//	{
//		INT32 rc = this->setAsVolumeMaster(this->m_as_handle, m_as_volume);
//		if (CSD_EOK != rc) {
//			fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
//					AUDIOSERVICE_PARAM_29);
//			LOGERR("setAsVolumeMaster(volumeLevel=%d) failed: 0x%d\r\n", level,
//					rc);
//		}
//	}
	unLock();
}

INT32 CsdOut::GetVolume() {
	INT32 nRet = 0;
	lock();
	nRet = m_iVolLevel;
	unLock();

	return nRet;
}

void CsdOut::SetMuteState(const uint32_t mutestate) {
	lock();
	m_iMuteState = mutestate;
	unLock();
}

int32_t CsdOut::initFile(const CHAR *strFileName, audio_src_t *pAudioSrc, INT32 channel, int alarmId) {
	int rc = 0;

	CSystemData *pData = CSystemData::GetInstance();
	/* Open audio file */
	pAudioSrc->pFile = fopen(strFileName, "r");
	if (0 == pAudioSrc->pFile) {
		LOGERR("file(%s) open failed\r\n", strFileName);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_35);
		rc = -1;
		return rc;
	}

	bool bigEndian = false;
	/* Validate wave header */
	rc = this->checkHdr(pAudioSrc->pFile, &bigEndian);
	if (0 != rc) {
		LOGERR("file header checked failed\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_36);
		fclose(pAudioSrc->pFile);
		return rc;
	}

	int fmtLength = 0;
	fmtLength = this->findTag(pAudioSrc->pFile, kFmtId, bigEndian);
	if (0 == fmtLength || fmtLength < (int) sizeof(WaveHdr)) {
		LOGERR("invalid wav file\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_37);
		fclose(pAudioSrc->pFile);
		rc = -1;
		return rc;
	}

	WaveHdr wavHdr;

	size_t count = 1;
	size_t size = 0;
	size = fread(&wavHdr, sizeof(wavHdr), count, pAudioSrc->pFile);
	if (size < count) {
		LOGERR("invalid wav file\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_38);
		fclose(pAudioSrc->pFile);
		rc = -1;
		return rc;
	}

	/* Some files have extra data in fmt field, so skip past it */
	if (fmtLength > (int) sizeof(WaveHdr)) {
		if (fseek(pAudioSrc->pFile, (fmtLength - (int) sizeof(WaveHdr)),
				SEEK_CUR) != 0) {
			LOGDBG("fseek failed\r\n");
			fclose(pAudioSrc->pFile);
			rc = -1;
			return rc;
		}
	}

	if (bigEndian) {
		pAudioSrc->sampleChannels = ENDIAN_BE16(wavHdr.Channels);
		pAudioSrc->sampleRate = ENDIAN_BE32(wavHdr.SamplesPerSec);
		pAudioSrc->byteRate = ENDIAN_BE32(wavHdr.AvgBytesPerSec);
		pAudioSrc->sampleBlockAlign = ENDIAN_BE16(wavHdr.BlockAlign);
		pAudioSrc->sampleBits = ENDIAN_BE16(wavHdr.BitsPerSample);
	} else {
		pAudioSrc->sampleChannels = ENDIAN_LE16(wavHdr.Channels);
		pAudioSrc->sampleRate = ENDIAN_LE32(wavHdr.SamplesPerSec);
		pAudioSrc->byteRate = ENDIAN_LE32(wavHdr.AvgBytesPerSec);
		pAudioSrc->sampleBlockAlign = ENDIAN_LE16(wavHdr.BlockAlign);
		pAudioSrc->sampleBits = ENDIAN_LE16(wavHdr.BitsPerSample);
	}

	pAudioSrc->sampleWordBits = pAudioSrc->sampleBlockAlign
			/ pAudioSrc->sampleChannels * 8;

	pAudioSrc->sampleSign = (pAudioSrc->sampleBits <= 8) ? 0 : 1;
	pAudioSrc->sampleInterleave = (pAudioSrc->sampleChannels <= 1) ? 0 : 1;
	pAudioSrc->sampleEndianness = bigEndian ? 1 : 0;
	pAudioSrc->sampleMode = 0;

	//if (pAudioSrc->sampleWordBits > 16)
	if (pAudioSrc->sampleWordBits > 32)
	{
		pAudioSrc->csdFormat = CSD_FORMAT_MULTI_CHANNEL_PCM_V2;
	}
	else
	{
//		pAudioSrc->csdFormat = CSD_FORMAT_PCM;
		// HS7012A 默认是用formt = 28 Ddv_id =410  4通道
		if((1 == pData->GetPowerType()) && (308 <= alarmId && alarmId <= 325))  // 配置外置功放,雷达报警音
		{
			pAudioSrc->csdFormat = CSD_FORMAT_PCM;
		}
		else
		{
			pAudioSrc->csdFormat = CSD_FORMAT_MULTI_CHANNEL_PCM;
		}
	}

	/*
	 * Buffer usage must be set so it contains the same number of samples
	 * for each channel
	 */
	if (NUM_CHANNELS_MONO == pAudioSrc->sampleChannels)
	{
		pAudioSrc->bufByteRead = BUFFER_POOL_BUF_SIZE / 4
				/ (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8)
				* (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8);
		pAudioSrc->bufByteUsed = pAudioSrc->bufByteRead * 4;
	}
	else if (NUM_CHANNELS_STEREO == pAudioSrc->sampleChannels)
	{
		pAudioSrc->bufByteRead = BUFFER_POOL_BUF_SIZE / 2
				/ (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8)
				* (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8);
		pAudioSrc->bufByteUsed = pAudioSrc->bufByteRead * 2;
	}
	else
	{
		pAudioSrc->bufByteRead = BUFFER_POOL_BUF_SIZE
				/ (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8)
				* (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8);
		pAudioSrc->bufByteUsed = pAudioSrc->bufByteRead;
	}

	pAudioSrc->dataReaded = 0;
	pAudioSrc->dataTotal = this->findTag(pAudioSrc->pFile, kDataId, bigEndian);
	pAudioSrc->dataPosition = ftell(pAudioSrc->pFile);

	uint64_t a = pAudioSrc->dataTotal * 8000ull;
	uint64_t b = pAudioSrc->sampleChannels * 1ull * pAudioSrc->sampleRate * pAudioSrc->sampleBits;
	if (b > 0L)
	{
		pAudioSrc->duration = a / b;
	}
	else
	{
		pAudioSrc->duration = 0L;
	}

//	if(REF_CHANNEL != channel) // 限制ref参考音
//	{
//		LOGINF("sampleChannels=%d",pAudioSrc->sampleChannels);
//		LOGINF("sampleRate=%d",pAudioSrc->sampleRate);
//		LOGINF("byteRate=%d",pAudioSrc->byteRate);
//		LOGINF("sampleBlockAlign=%d",pAudioSrc->sampleBlockAlign);
//		LOGINF("sampleBits=%d",pAudioSrc->sampleBits);
//		LOGINF("sampleWordBits=%d",pAudioSrc->sampleWordBits);
//		LOGINF("sampleSign=%d",pAudioSrc->sampleSign);
//		LOGINF("sampleInterleave=%d",pAudioSrc->sampleInterleave);
//		LOGINF("sampleMode=%d",pAudioSrc->sampleMode);
//		LOGINF("csdFormat=%d",pAudioSrc->csdFormat);
//		LOGINF("bufByteUsed=%d",pAudioSrc->bufByteUsed);
//		LOGINF("bufByteRead=%d",pAudioSrc->bufByteRead);
//		LOGINF("dataTotal=%d",pAudioSrc->dataTotal);
//		LOGINF("dataPosition=%d",pAudioSrc->dataPosition);
//	}
	return rc;
}

int32_t CsdOut::initFile_PCM(const CHAR *strFileName, audio_src_t *pAudioSrc, INT32 channel, int alarmId) {
	int rc = 0;

	/* Open audio file */
	pAudioSrc->pFile = fopen(strFileName, "r");
	if (0 == pAudioSrc->pFile) {
		LOGERR("file(%s) open failed\r\n", strFileName);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_35);
		rc = -1;
		return rc;
	}

	fseek(pAudioSrc->pFile, 0, SEEK_END);
	pAudioSrc->dataTotal = ftell(pAudioSrc->pFile);
	 fseek(pAudioSrc->pFile, 0, SEEK_SET);

	pAudioSrc->sampleChannels = 1;
	pAudioSrc->sampleRate = 24000;
	pAudioSrc->sampleBits = 16;

	pAudioSrc->sampleBlockAlign = pAudioSrc->sampleChannels * pAudioSrc->sampleBits /8;
	pAudioSrc->byteRate = pAudioSrc->sampleRate * pAudioSrc->sampleChannels * pAudioSrc->sampleBits /8;
	pAudioSrc->sampleWordBits = pAudioSrc->sampleBlockAlign / pAudioSrc->sampleChannels * 8;

	pAudioSrc->sampleSign = (pAudioSrc->sampleBits <= 8) ? 0 : 1;
	pAudioSrc->sampleInterleave = (pAudioSrc->sampleChannels <= 1) ? 0 : 1;
	pAudioSrc->sampleEndianness = 1;
	pAudioSrc->sampleMode = 0;
	//pAudioSrc->csdFormat = CSD_FORMAT_PCM/*CSD_FORMAT_MULTI_CHANNEL_PCM*/;
	pAudioSrc->csdFormat = CSD_FORMAT_MULTI_CHANNEL_PCM;

	if (NUM_CHANNELS_MONO == pAudioSrc->sampleChannels)
	{
		pAudioSrc->bufByteRead = BUFFER_POOL_BUF_SIZE / 4
				/ (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8)
				* (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8);
		pAudioSrc->bufByteUsed = pAudioSrc->bufByteRead * 4;
	}
	else if (NUM_CHANNELS_STEREO == pAudioSrc->sampleChannels)
	{
		pAudioSrc->bufByteRead = BUFFER_POOL_BUF_SIZE / 2
				/ (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8)
				* (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8);
		pAudioSrc->bufByteUsed = pAudioSrc->bufByteRead * 2;
	}
	else
	{
		pAudioSrc->bufByteRead = BUFFER_POOL_BUF_SIZE
				/ (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8)
				* (pAudioSrc->sampleChannels * pAudioSrc->sampleBits / 8);
		pAudioSrc->bufByteUsed = pAudioSrc->bufByteRead;
	}
	pAudioSrc->dataReaded = 0;
	pAudioSrc->dataPosition = 0;
	uint64_t a = pAudioSrc->dataTotal * 8000ull;
	uint64_t b = pAudioSrc->sampleChannels * 1ull * pAudioSrc->sampleRate * pAudioSrc->sampleBits;
	if (b > 0L)
	{
		pAudioSrc->duration = a / b;
	}
	else
	{
		pAudioSrc->duration = 0L;
	}

//	if(REF_CHANNEL != channel) // 限制ref参考音
//	{
//		LOGINF("sampleChannels=%d",pAudioSrc->sampleChannels);
//		LOGINF("sampleRate=%d",pAudioSrc->sampleRate);
//		LOGINF("byteRate=%d",pAudioSrc->byteRate);
//		LOGINF("sampleBlockAlign=%d",pAudioSrc->sampleBlockAlign);
//		LOGINF("sampleBits=%d",pAudioSrc->sampleBits);
//		LOGINF("sampleWordBits=%d",pAudioSrc->sampleWordBits);
//		LOGINF("sampleSign=%d",pAudioSrc->sampleSign);
//		LOGINF("sampleInterleave=%d",pAudioSrc->sampleInterleave);
//		LOGINF("sampleMode=%d",pAudioSrc->sampleMode);
//		LOGINF("csdFormat=%d",pAudioSrc->csdFormat);
//		LOGINF("bufByteUsed=%d",pAudioSrc->bufByteUsed);
//		LOGINF("dataTotal=%d",pAudioSrc->dataTotal);
//		LOGINF("dataPosition=%d",pAudioSrc->dataPosition);
//	}
	return rc;
}

int32_t CsdOut::initCsd(const int audioZone, const int channel, int alarmId) {
	int32_t rc = CSD_EOK;
	uint32_t ac_category = CSD_AC_CATEGORY_GENERIC_PLAYBACK;
	CSystemData *pData = CSystemData::GetInstance();
	int nPowerCfg = pData->GetPowerType();

	LOGINF("(%d)nPowerCfg: %d, alarmId: %d", channel,nPowerCfg,  alarmId);

	/**
	 * 根据音区id匹配对应的设备Id及其左右单声道
	 */
	if(1 == nPowerCfg)  // 外置功放
	{
		if(INSIDE_SPEAKER == channel)
		{
			if(308 <= alarmId && alarmId <= 325)
			{
				this->SetDevId(CSD_OEM_QUAT_TDM_RX_CHIME_1CH);//TODO: 外置功放,雷达通道415
				this->m_as_channel = CHANNEL_STEREO_FLFR_RLRR;
			}
			else
			{
				this->SetDevId(CSD_OEM_SEC_TDM_RX_CHIME_4CH);//TODO: for test by 7012A
				//this->m_as_channel = audioZone;
				//this->m_as_channel = CHANNEL_STEREO_FLFR_RLRR;  //外置功放,需要往四个通道推送数据
				this->m_as_channel = CHANNEL_STEREO_FL_FR;  //外置功放,需要往四个通道推送数据
			}
		}
		else if(OUTSIDE_SPEAKER == channel)
		{
			this->SetDevId(CSD_OEM_SEC_TDM_RX_AVAS_2CH);//TODO: for test by 7012A
			//this->SetDevId(CSD_OEM_SEC_TDM_RX_TTS_2CH);//TODO: for test by 7012A
			this->m_as_channel = OUTSIDE_SPEAKER_TWO;
		}
/*		else if(POWER_RADAR == channel)
		{
			this->SetDevId(CSD_OEM_QUAT_TDM_RX_CHIME_1CH);//TODO: for test by 7012A
			this->m_as_channel = CHANNEL_STEREO_FLFR_RLRR;
		}*/
		else if(REF_CHANNEL == channel)  // 参考音通道
		{
			this->SetDevId(CSD_OEM_PRI_TDM_RX_REF_4CH);//TODO: for test by 7012A
			this->m_as_channel = CHANNEL_STEREO_FLFR_RLRR;
		}
		else
		{
			LOGERR("invaild channel error %d", channel);
		}
	}
	else
	{
		if(INSIDE_SPEAKER == channel /*|| POWER_RADAR == channel*/)
		{
			switch(audioZone)
			{
				case CHANNEL_STEREO_FL:
				case CHANNEL_STEREO_FR:
				case CHANNEL_STEREO_RL:
				case CHANNEL_STEREO_RR:
				case CHANNEL_STEREO_FL_RL:
				case CHANNEL_STEREO_FR_RR:
				case CHANNEL_STEREO_FL_FR:
				case CHANNEL_STEREO_RL_RR:
				case CHANNEL_STEREO_FLFR_RLRR:
				case CHANNEL_STEREO_FR_RL:
				case CHANNEL_STEREO_FL_RR:
				case CHANNEL_STEREO_FLFR_RR:
				case CHANNEL_STEREO_FLFR_RL:
				case CHANNEL_STEREO_FL_RLRR:
				case CHANNEL_STEREO_FR_RLRR:
				{
					this->SetDevId(CSD_OEM_SEC_TDM_RX_CHIME_4CH);//TODO: for test by 7012A
					this->m_as_channel = audioZone;
					break;
				}
				default:
				{
					this->SetDevId(CSD_OEM_SEC_TDM_RX_CHIME_4CH);//TODO: for test by 7012A
					//this->SetDevId(CSD_OEM_QUAT_TDM_MULTI_LANE_SD1_8CH_SPKR);
					this->m_as_channel = CHANNEL_STEREO_FLFR_RLRR;
					LOGERR("invaild audioZone error: %d, Set audiozone (FLFR_RLRR 15)", audioZone);
					fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
							AUDIOSERVICE_PARAM_28);
					return -1;
					break;
				}
			}
		}
		else if(OUTSIDE_SPEAKER == channel)
		{
			switch(audioZone)
			{
				case OUTSIDE_SPEAKER_ONE:
				case OUTSIDE_SPEAKER_TWO:
				{
					this->SetDevId(CSD_OEM_SEC_TDM_RX_AVAS_2CH);//TODO: for test by 7012A
					this->m_as_channel = audioZone;
					break;
				}
				default:
				{
					this->SetDevId(CSD_OEM_SEC_TDM_RX_AVAS_2CH);//TODO: for test by 7012A
					this->m_as_channel = OUTSIDE_SPEAKER_TWO;
					LOGERR("invaild audioZone error: %d, Set audiozone (TWO 3)", audioZone);
					fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
							AUDIOSERVICE_PARAM_28);
					return -1;
					break;
				}
			}
		}
		else if(REF_CHANNEL == channel)  // 参考音通道
		{
			this->SetDevId(CSD_OEM_PRI_TDM_RX_REF_4CH);//TODO: for test by 7012A
			this->m_as_channel = CHANNEL_STEREO_FLFR_RLRR;
		}
		else
		{
			LOGERR("invaild channel error %d", channel);
		}
	}

	LOGINF("(%d)m_as_channel: %d", channel, m_as_channel);

	rc = csd_init();
	if (CSD_EOK != rc) {
		LOGERR("csd_init failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_0);
		return rc;
	}

	m_dev_handle = this->openDevice();
	if (0 == m_dev_handle) {
		rc = CSD_EFAILED;
		return rc;
	}

	rc = this->enableDevice(this->m_dev_id, DEV_SAMPLE_RATE, m_dev_sample_bits,
			m_dev_handle);
	if (CSD_EOK != rc) {
		this->closeDevice(this->m_dev_handle);
		this->m_dev_handle = 0;

		return rc;
	}

	rc = this->setDevVolumeMaster(m_dev_handle, this->m_dev_id,this->m_dev_volume);
	if (CSD_EOK != rc) {
		this->disableDevice(this->m_dev_id, this->m_dev_handle);
		this->closeDevice(this->m_dev_handle);
		this->m_dev_handle = 0;

		return rc;
	}

	 m_ac_handle = this->openAudioContextV2(this->m_dev_id,
			DEV_SAMPLE_RATE, m_dev_sample_bits, m_ac_open_mask, ac_category);
	if (0 == m_ac_handle) {
		this->disableDevice(this->m_dev_id, this->m_dev_handle);
		this->closeDevice(this->m_dev_handle);
		this->m_dev_handle = 0;
		rc = CSD_EFAILED;
		return rc;
	}

//	rc = this->enableContext(m_ac_handle);
//	if (CSD_EOK != rc) {
//		this->disableDevice(this->m_dev_id, this->m_dev_handle);
//
//		this->closeContext(this->m_ac_handle);
//		this->m_ac_handle = 0;
//
//		this->closeDevice(this->m_dev_handle);
//		this->m_dev_handle = 0;
//
//		return rc;
//	}

	return rc;
}


int32_t CsdOut::setupCsdSession(audio_src_t *pAudioSrc, int audio_channel, int alarmId) {
	int rc = CSD_EOK;

	uint32_t buf_mem_type = CSD_AS_BUF_MEM_SHARED;
	uint32_t format = pAudioSrc->csdFormat;
	int32_t sampleChannels = pAudioSrc->sampleChannels;

	uint8_t channel_mapping_playback[MAX_NUM_CHANNELS] = {
			CSD_AS_PCM_MULTI_CHANNEL_FL, CSD_AS_PCM_MULTI_CHANNEL_FR,
			CSD_AS_PCM_MULTI_CHANNEL_FC, CSD_AS_PCM_MULTI_CHANNEL_LFE,
			CSD_AS_PCM_MULTI_CHANNEL_LB, CSD_AS_PCM_MULTI_CHANNEL_RB,
			CSD_AS_PCM_MULTI_CHANNEL_LS, CSD_AS_PCM_MULTI_CHANNEL_RS };

	uint8_t channel_mapping_v2_playback[MAX_NUM_CHANNELS_V2] = {
			CSD_AS_PCM_MULTI_CHANNEL_FL, CSD_AS_PCM_MULTI_CHANNEL_FR,
			CSD_AS_PCM_MULTI_CHANNEL_FC, CSD_AS_PCM_MULTI_CHANNEL_LS,
			CSD_AS_PCM_MULTI_CHANNEL_RS, CSD_AS_PCM_MULTI_CHANNEL_LFE,
			CSD_AS_PCM_MULTI_CHANNEL_CS, CSD_AS_PCM_MULTI_CHANNEL_LB,
			CSD_AS_PCM_MULTI_CHANNEL_RB, CSD_AS_PCM_MULTI_CHANNEL_TS,
			CSD_AS_PCM_MULTI_CHANNEL_CVH, CSD_AS_PCM_MULTI_CHANNEL_MS,
			CSD_AS_PCM_MULTI_CHANNEL_FLC, CSD_AS_PCM_MULTI_CHANNEL_FRC,
			CSD_AS_PCM_MULTI_CHANNEL_RLC, CSD_AS_PCM_MULTI_CHANNEL_RRC,
			CSD_AS_PCM_MULTI_CHANNEL_LFE2, CSD_AS_PCM_MULTI_CHANNEL_SL,
			CSD_AS_PCM_MULTI_CHANNEL_SR, CSD_AS_PCM_MULTI_CHANNEL_TFL,
			CSD_AS_PCM_MULTI_CHANNEL_TFR, CSD_AS_PCM_MULTI_CHANNEL_TC,
			CSD_AS_PCM_MULTI_CHANNEL_TBL, CSD_AS_PCM_MULTI_CHANNEL_TBR,
			CSD_AS_PCM_MULTI_CHANNEL_TSL, CSD_AS_PCM_MULTI_CHANNEL_TSR,
			CSD_AS_PCM_MULTI_CHANNEL_TBC, CSD_AS_PCM_MULTI_CHANNEL_BFC,
			CSD_AS_PCM_MULTI_CHANNEL_BFL, CSD_AS_PCM_MULTI_CHANNEL_BFR,
			CSD_AS_PCM_MULTI_CHANNEL_LW, CSD_AS_PCM_MULTI_CHANNEL_RW };

	uint8_t *channel_mapping = channel_mapping_playback;
	uint8_t *channel_mapping_v2 = channel_mapping_v2_playback;
	CSystemData *pData = CSystemData::GetInstance();

	LOGINF("(%d)PowerCfg:%d",audio_channel,pData->GetPowerType());
	LOGINF("(%d)m_dev_channels:%d,format:%d,sampleChannels:%d ",audio_channel,m_dev_channels,format,pAudioSrc->sampleChannels);

	m_as_handle = this->openAudioStreamV2(buf_mem_type, pAudioSrc->sampleBits, format, m_as_open_mask);
	if (0 == m_as_handle) {
		LOGERR("setupCsdSession => openAudioStreamV2 failed!");
		rc = CSD_EFAILED;
		return rc;
	}

	if (2 < m_dev_channels && m_dev_channels <= MAX_NUM_CHANNELS_V2)
	{
		rc = setStreamContextMultichannel(m_as_handle, m_ac_handle,m_dev_channels,
				m_dev_channels > MAX_NUM_CHANNELS ? channel_mapping_v2 : channel_mapping);
		if (CSD_EOK != rc)
		{
			LOGERR("setStreamContextMultichannel failed: 0x%x", rc);
			this->closeStream(this->m_as_handle);
			this->m_as_handle = 0;
			return rc;
		}
	}

	/***************************************************************/
	rc = this->streamAttach(m_ac_handle, m_as_handle);
	if (CSD_EOK != rc)
	{
		freeCsdBufferPool(m_as_handle, m_buffer_pool, m_buffer_pool_count,
				m_buffer_pool_buf_size);

		this->closeStream(this->m_as_handle);
		this->m_as_handle = 0;
		return rc;
	}
	/***************************************************************/
	rc = this->enableContext(m_ac_handle);
	if (CSD_EOK != rc) {
		this->disableDevice(this->m_dev_id, this->m_dev_handle);

		this->closeContext(this->m_ac_handle);
		this->m_ac_handle = 0;

		this->closeDevice(this->m_dev_handle);
		this->m_dev_handle = 0;

		return rc;
	}
	/***************************************************************/

	if (CSD_FORMAT_MULTI_CHANNEL_PCM_V2 == format)
	{
		if (NUM_CHANNELS_MONO == sampleChannels
				&& (CHANNEL_STEREO_LEFT == m_as_channel
						|| CHANNEL_STEREO_RIGTH == m_as_channel))
		{
			rc = setStreamFormatPcmMultichannelV2(m_as_handle,
					pAudioSrc->sampleRate,
					NUM_CHANNELS_STEREO, pAudioSrc->sampleBits,
					pAudioSrc->sampleSign, 1, pAudioSrc->sampleWordBits,
					pAudioSrc->sampleEndianness, pAudioSrc->sampleMode,
					channel_mapping_v2);
		}
		else
		{
			rc = setStreamFormatPcmMultichannelV2(m_as_handle,
					pAudioSrc->sampleRate, sampleChannels,
					pAudioSrc->sampleBits, pAudioSrc->sampleSign,
					pAudioSrc->sampleInterleave, pAudioSrc->sampleWordBits,
					pAudioSrc->sampleEndianness, pAudioSrc->sampleMode,
					channel_mapping_v2);
		}
	}
	else if (CSD_FORMAT_MULTI_CHANNEL_PCM == format)
	{
//		rc = setStreamFormatPcmMultichannel(m_as_handle, pAudioSrc->sampleRate, 2,
//		pAudioSrc->sampleBits, pAudioSrc->sampleSign,pAudioSrc->sampleInterleave, channel_mapping);
		CSystemData *pData = CSystemData::GetInstance();
		if(INSIDE_SPEAKER == audio_channel || REF_CHANNEL == audio_channel /*|| POWER_RADAR == audio_channel*/)
		{
			if(1 != pData->GetPowerType())
			{
				m_as_volume = pData->GetVolumeLevel();
				LOGINF("(%d)setAsVolumeMaster---m_as_volume:%d ",audio_channel,m_as_volume);
				rc = this->setAsVolumeMaster(m_as_handle, m_as_volume);  // 设置音量大小
				if (CSD_EOK != rc)
				{
					fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
							AUDIOSERVICE_PARAM_29);
					LOGERR("setAsVolumeMaster(volumeLevel=%d) failed: 0x%d\r\n", m_as_volume,rc);
				}
			}
		}
		else if(OUTSIDE_SPEAKER == audio_channel || REF_CHANNEL == audio_channel)
		{
			LOGINF("(%d)m_as_outvolume:%d, alarmId: %d",audio_channel, m_as_outvolume,alarmId);
			if(352 == alarmId || 351 == alarmId || 999 == alarmId)
			{
				rc = this->setAsVolumeMaster(m_as_handle, 80);  // 设置音量大小
				if (CSD_EOK != rc)
				{
					fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
							AUDIOSERVICE_PARAM_29);
					LOGERR("setAsVolumeMaster(volumeLevel=%d) failed: 0x%d\r\n", m_as_outvolume,rc);
				}
			}
			else
			{
				m_as_outvolume = pData->GetOutSideVolumeLevel();
				rc = this->setAsVolumeMaster(m_as_handle, m_as_outvolume);  // 设置音量大小
				if (CSD_EOK != rc)
				{
					fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
							AUDIOSERVICE_PARAM_29);
					LOGERR("setAsVolumeMaster(volumeLevel=%d) failed: 0x%d\r\n", m_as_outvolume,rc);
				}
			}
		}
		rc = setStreamFormatPcmMultichannel(m_as_handle, pAudioSrc->sampleRate, 4,
						pAudioSrc->sampleBits, pAudioSrc->sampleSign,pAudioSrc->sampleInterleave, channel_mapping);
	}
	else
	{
		if (NUM_CHANNELS_MONO == sampleChannels)
		{
			rc = this->setStreamFormat2(m_as_handle,
					pAudioSrc->sampleRate,
					sampleChannels, pAudioSrc->sampleBits,
					pAudioSrc->sampleSign, pAudioSrc->sampleInterleave);
		}
		else
		{
			rc = this->setStreamFormat2(m_as_handle,
					pAudioSrc->sampleRate, sampleChannels,
					pAudioSrc->sampleWordBits, pAudioSrc->sampleSign,
					pAudioSrc->sampleInterleave);
		}
	}
	if (CSD_EOK != rc)
	{
		this->closeStream(this->m_as_handle);
		this->m_as_handle = 0;
		return rc;
	}

	/***************************************************************/
//	int param_vol = 99;
//	uint32_t vol = (uint32_t)param_vol;
//	rc = this->setAsVolumeMaster(m_as_handle, vol);  // 设置音量大小
//	if (CSD_EOK != rc)
//	{
//		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_PARAM_NULL, LEVEL_WARNING,
//				AUDIOSERVICE_PARAM_29);
//		LOGERR("setAsVolumeMaster(volumeLevel=%d) failed: 0x%d\r\n", param_vol,
//				rc);
//	}
	/***************************************************************/


	rc = this->registerCallBack(m_as_handle,
			(void *) CsdOut::csdUserCallBack, this);
	if (CSD_EOK != rc)
	{
		this->closeStream(this->m_as_handle);
		this->m_as_handle = 0;
		return rc;
	}

	rc = allocateCsdBufferPool(m_as_handle, m_buffer_pool, m_buffer_pool_count,
			m_buffer_pool_buf_size);
	if (CSD_EOK != rc)
	{
		this->closeStream(this->m_as_handle);
		this->m_as_handle = 0;
		return rc;
	}

//	rc = this->streamAttach(m_ac_handle, m_as_handle);
//	if (CSD_EOK != rc)
//	{
//		freeCsdBufferPool(m_as_handle, m_buffer_pool, m_buffer_pool_count,
//				m_buffer_pool_buf_size);
//
//		this->closeStream(this->m_as_handle);
//		this->m_as_handle = 0;
//		return rc;
//	}

	rc = this->setStartSession(m_as_handle);
	if (CSD_EOK != rc)
	{
		freeCsdBufferPool(m_as_handle, m_buffer_pool, m_buffer_pool_count,
				m_buffer_pool_buf_size);

		this->streamDetach(m_ac_handle, m_as_handle);

		this->closeStream(this->m_as_handle);
		this->m_as_handle = 0;
	}

	return rc;
}


int32_t CsdOut::streamCsdSession(audio_src_t *pAudioSrc, int repeat_count,
		int min_count, int interval_time, int audio_channel, int alarm_id) {
	int32_t retval = CSD_EOK;
	CSystemData *pData = CSystemData::GetInstance();
	int powercfg = pData->GetPowerType();
	uint32_t index = 0;
	uint32_t bytes_read = 0;
	FILE* output_file = NULL;
	int play_count = 0;
	struct csd_as_buffer as_buffer;

	memset((void *) &as_buffer, 0, sizeof(struct csd_as_buffer));

	_ulong starttime = getTickms();

	if(REF_CHANNEL != audio_channel)
	{
//		if (( output_file = fopen ("/fs/data/AudioService.pcm", "wb")) == 0)
//		{
//			LOGERR("PCM_FILE_NAME open failed\n");
//		}
	}

	LOGINF("(%d)Play Audio Stream Start[%llums]...", audio_channel,starttime);
	LOGINF("(%d)alarm_id:%d", audio_channel,alarm_id);
	while (1)
	{

		pthread_mutex_lock(&m_csd_mutex);
		{
//			if(this->m_bExit == 1 && audio_channel == 1)
//			{
//				LOGINF("(%d)m_bExit(%d)--dataReaded(%d)---dataTotal(%d)", audio_channel, this->m_bExit, pAudioSrc->dataReaded,pAudioSrc->dataTotal);
//			}
			if(alarm_id == 303 || alarm_id == 304) // 转向灯声音音源文件是特殊处理，可以直接打断退出。不用等待本次音频文件写完
			{
				if((this->m_bStop && play_count >= min_count)
											|| this->m_bExit)
				{
					m_csd_event_type |= CSD_SWITCH_OFF_EVENT;
				}

				if ((m_csd_event_type & CSD_SWITCH_OFF_EVENT))
				{
					pthread_mutex_unlock(&m_csd_mutex);
					break;
				}
			}
			else
			{
				if (pAudioSrc->dataReaded >= pAudioSrc->dataTotal) // 等待本次音频文件写完，磁能退出;对策打断后出现pop音问题
				{
					if((this->m_bStop && play_count >= min_count)
							|| this->m_bExit)
					{
						m_csd_event_type |= CSD_SWITCH_OFF_EVENT;
					}

					if ((m_csd_event_type & CSD_SWITCH_OFF_EVENT))
					{
						pthread_mutex_unlock(&m_csd_mutex);
						break;
					}
				}
			}
		}
		pthread_mutex_unlock(&m_csd_mutex);

		/* Reserve the next available PMEM buffer from pool */
		do {
			retval = reserveCsdBufferFromPool(m_buffer_pool,
					m_buffer_pool_count, m_buffer_pool_free_count, &index);

			if (0 != retval) {

				LOGDBG(
						"streamCsdSession => reserveCsdBufferFromPool() did not return buffer, watiting for CSD_AS_EVT_BUF_DONE event");

				pthread_mutex_lock(&m_csd_mutex);
				{
					if (m_csd_event_type & CSD_BUF_DONE_EVENT) {
						m_csd_event_type &= ~CSD_BUF_DONE_EVENT;
					} else {
						pthread_cond_wait(&m_csd_cond, &m_csd_mutex);
						if (m_csd_event_type & CSD_BUF_DONE_EVENT) {
							m_csd_event_type &= ~CSD_BUF_DONE_EVENT;
						}
					}
				}
				pthread_mutex_unlock(&m_csd_mutex);

				LOGDBG(
						"streamCsdSession => CSD_AS_EVT_BUF_DONE event received, attempting to reserve a buffer again");

				retval = reserveCsdBufferFromPool(m_buffer_pool,
						m_buffer_pool_count, m_buffer_pool_free_count, &index);
			}
		} while (0 != retval);

		as_buffer.buf_addr = m_buffer_pool[index].pPa;
		as_buffer.buf_size = pAudioSrc->bufByteUsed;
		memset(m_buffer_pool[index].pVa, 0x0, as_buffer.buf_size);

//
//		LOGINF(
//				"streamCsdSession => PMEM buffer found, pPa = 0x%p, pVa = 0x%p, as_buffer.size = %d bytes",
//				m_buffer_pool[index].pPa, m_buffer_pool[index].pVa,
//				as_buffer.buf_size);


		/* Read buffer from file */
		memset(m_buffer_pool[index].pVa, 0x0, as_buffer.buf_size);


		if(INSIDE_SPEAKER == audio_channel || REF_CHANNEL == audio_channel/*|| POWER_RADAR == audio_channel*/)
		{
			if(1 != powercfg)
			{
				uint32_t vol = pData->GetVolumeLevel();
				if(m_as_volume != vol)
				{
					LOGINF("(%d)set Volume(%d)", audio_channel,vol);
					this->setAsVolumeMaster(m_as_handle, vol);  // 设置音量大小
					m_as_volume = vol;
				}
			}
		}
		else if(OUTSIDE_SPEAKER == audio_channel || REF_CHANNEL == audio_channel)
		{
			if(352 == alarm_id || 351 == alarm_id || 999 == alarm_id) // 车外声音特殊逻辑处理：闭锁成功、闭锁失败提示、语音播放音量不可调
			{
				this->setAsVolumeMaster(m_as_handle, 80);  // 设置音量大小
			}
			else
			{
				uint32_t outvol = pData->GetOutSideVolumeLevel();
				if(m_as_outvolume != outvol)
				{
					LOGINF("(%d)set Volume(%d)", audio_channel,outvol);
					this->setAsVolumeMaster(m_as_handle, outvol);  // 设置音量大小
					m_as_outvolume = outvol;
				}
			}
		}

//			/*******************************************************************/
//			// 直接拷贝数据不处理
//			uint32_t remainder = pAudioSrc->dataTotal - pAudioSrc->dataReaded;
//			bytes_read = fread(m_buffer_pool[index].pVa, 1,
//					(pAudioSrc->bufByteUsed < remainder ?
//							pAudioSrc->bufByteUsed : remainder), pAudioSrc->pFile);
//
//			pAudioSrc->dataReaded += bytes_read;
//			/*******************************************************************/

		//bytes_read = this->processStream(m_buffer_pool[index].pVa, pAudioSrc);
		if((1 == powercfg) && (308 <= alarm_id && alarm_id <= 325))  // 配置外置功放,雷达报警音
		{
			// 直接拷贝数据不处理
			uint32_t remainder = pAudioSrc->dataTotal - pAudioSrc->dataReaded;
			bytes_read = fread(m_buffer_pool[index].pVa, 1,
					(pAudioSrc->bufByteUsed < remainder ?
							pAudioSrc->bufByteUsed : remainder), pAudioSrc->pFile);
			pAudioSrc->dataReaded += bytes_read;
		}
		else
		{
			bytes_read = this->processStream_7012A(m_buffer_pool[index].pVa, pAudioSrc, output_file, audio_channel);
		}
		as_buffer.buf_size = bytes_read;

		/* Write PMEM buffer to CSD driver */
		retval = csd_write(m_as_handle, (void *) &as_buffer, sizeof(as_buffer));

		if (as_buffer.buf_size != retval) {
			if (-1 == retval) {
				/* An error occured, so release PMEM buffer. */
				releaseCsdBufferFromPool(m_buffer_pool, m_buffer_pool_count,
						m_buffer_pool_free_count, m_buffer_pool[index].pPa,
						NULL, 0);

				LOGINF("streamCsdSession => Releasing PMEM buffer as csd_write() returned -1");
			}
		}

		if (pAudioSrc->dataReaded >= pAudioSrc->dataTotal) {

			play_count++;

			if (play_count < repeat_count || 0xFF == repeat_count)
			{

				if ((this->m_bStop && play_count >= min_count)
						|| this->m_bExit)
				{
					pthread_mutex_lock(&m_csd_mutex);
					{
						m_csd_event_type |= CSD_SWITCH_OFF_EVENT;
					}
					pthread_mutex_unlock(&m_csd_mutex);
					break;
				}

				// C857 声音提示无延迟要求
				if (interval_time > 0) {
					_ulong writeovertime = getTickms();
					_ulong flushovertime = getTickms();

					static _ulong lastovertime = 0;
					long long writetime =
							(lastovertime == 0) ?
									(writeovertime - starttime) :
									(writeovertime - lastovertime);
					long long flushtime = flushovertime - writeovertime;

					long long ciInterval = interval_time;
					long long misstime = writetime + flushtime - pAudioSrc->duration;
					if (interval_time > misstime) {
						ciInterval = interval_time - misstime;
						ciInterval = ciInterval
								- ((ciInterval / 10) * 1 * (10.0 / 11.0));
					}

					long long temp = 0;
					bool bBreak = false;
					while (temp < ciInterval) {
						delay(10);
						if ((this->m_bStop && play_count >= min_count)
								|| this->m_bExit) {
							pthread_mutex_lock(&m_csd_mutex);
							{
								m_csd_event_type |= CSD_SWITCH_OFF_EVENT;
							}
							pthread_mutex_unlock(&m_csd_mutex);
							bBreak = true;
							break;
						}
						temp += 10;
					}

					lastovertime = getTickms();
					if (bBreak) {
						break;
					}
				}
				pAudioSrc->dataReaded = 0;
				if (fseek(pAudioSrc->pFile, pAudioSrc->dataPosition, SEEK_SET)!= 0)
				{
					LOGERR("fseek to dataPosition failed\r\n");
					break;
				}
				continue;
			}
			break;
		}
	}

	retval = csd_ioctl(m_as_handle, CSD_AS_CMD_SET_STREAM_EOS, NULL, 0);
	if (CSD_EOK != retval) {
		LOGERR(
				"streamCsdSession => csd_ioctl(CSD_AS_CMD_SET_STREAM_EOS) failed: 0x%x",
				retval);
	}

	if(REF_CHANNEL != audio_channel) // 限制ref参考音 不录制
	{
//		if(NULL != output_file)
//		{
//			fclose(output_file);
//			output_file = NULL;
//		}
	}
	LOGINF("(%d)Play Audio End[%llums]", audio_channel,getTickms());
	return retval;
}


uint32_t CsdOut::processStream(uint8_t *pVa, audio_src_t *pAudioSrc) {
	uint32_t bytes_read = 0;
	uint32_t remainder = pAudioSrc->dataTotal - pAudioSrc->dataReaded;

	if (!(CHANNEL_STEREO_LEFT == this->m_as_channel
			|| CHANNEL_STEREO_RIGTH == this->m_as_channel)) {
		bytes_read = fread(pVa, 1,
				(pAudioSrc->bufByteUsed < remainder ?
						pAudioSrc->bufByteUsed : remainder), pAudioSrc->pFile);

		pAudioSrc->dataReaded += bytes_read;
		return bytes_read;
	}

	int32_t sampleBytes = pAudioSrc->sampleBits / 8;

	LOGINF("m_as_channel: %d, sampleBytes:%d  dualBlockAlign:%d ", this->m_as_channel, sampleBytes, pAudioSrc->sampleBlockAlign * 2);
	LOGINF("bufByteUsed: %d, bufByteRead:%d, sampleChannels:%d", pAudioSrc->bufByteUsed, pAudioSrc->bufByteRead, pAudioSrc->sampleChannels);
	if (NUM_CHANNELS_MONO == pAudioSrc->sampleChannels)  // 单声道
	{
		int32_t dualBlockAlign = pAudioSrc->sampleBlockAlign * 2;

		uint8_t origArr[pAudioSrc->bufByteRead];
		memset(origArr, 0x0, pAudioSrc->bufByteRead);

		bytes_read = fread(origArr, 1,
				(pAudioSrc->bufByteRead < remainder ?
						pAudioSrc->bufByteRead : remainder), pAudioSrc->pFile);

		uint8_t resArr[pAudioSrc->bufByteUsed];
		memset(resArr, 0x0, pAudioSrc->bufByteUsed);

		switch (this->m_as_channel)
		{
		case CHANNEL_STEREO_LEFT:
			for (uint32_t i = 0, j = 0; i < bytes_read;)
			{
				memcpy(&resArr[j], &origArr[i], sampleBytes);
				i += pAudioSrc->sampleBlockAlign;
				j += dualBlockAlign;
			}
			break;
		default: //CHANNEL_STEREO_RIGTH
			for (uint32_t i = 0, j = 0; i < bytes_read;)
			{
				memcpy(&resArr[j + pAudioSrc->sampleBlockAlign], &origArr[i],
						sampleBytes);
				i += pAudioSrc->sampleBlockAlign;
				j += dualBlockAlign;
			}
			break;
		}

		pAudioSrc->dataReaded += bytes_read;

		memcpy(pVa, &resArr[0], pAudioSrc->bufByteUsed);

		bytes_read *= 2;
	}
	else
	{

		uint8_t origArr[pAudioSrc->bufByteRead];
		memset(origArr, 0x0, pAudioSrc->bufByteRead);

		bytes_read = fread(origArr, 1,
				(pAudioSrc->bufByteUsed < remainder ?
						pAudioSrc->bufByteUsed : remainder), pAudioSrc->pFile);

		uint8_t resArr[pAudioSrc->bufByteUsed];
		memset(resArr, 0x0, pAudioSrc->bufByteUsed);

		switch (this->m_as_channel)
		{
		case CHANNEL_STEREO_LEFT:
			for (uint32_t i = 0; i < bytes_read;)
			{
				memset(&origArr[i + sampleBytes], 0x0,
						(pAudioSrc->sampleChannels - 1) * sampleBytes);
				i += pAudioSrc->sampleBlockAlign;
			}
			break;
		default: //CHANNEL_STEREO_RIGTH
			if (pAudioSrc->sampleChannels == NUM_CHANNELS_STEREO)
			{
				for (uint32_t i = 0; i < bytes_read;)
				{
					memset(&origArr[i], 0x0, sampleBytes);
					i += pAudioSrc->sampleBlockAlign;
				}
			}
			else
			{
				for (uint32_t i = 0; i < bytes_read;)
				{
					memset(&origArr[i], 0x0, sampleBytes);
					memset(&origArr[i + 2 * sampleBytes], 0x0,
							(pAudioSrc->sampleChannels - 2) * sampleBytes);
					i += pAudioSrc->sampleBlockAlign;
				}
			}
			break;
		}

		pAudioSrc->dataReaded += bytes_read;

		memcpy(pVa, &origArr[0], pAudioSrc->bufByteUsed);
	}

	return bytes_read;
}

uint32_t CsdOut::processStream_7012A(uint8_t *pVa, audio_src_t *pAudioSrc, FILE* pfile, int channel)
{
	uint32_t bytes_read = 0;
	uint32_t remainder = pAudioSrc->dataTotal - pAudioSrc->dataReaded;

	if (!(0 < this->m_as_channel && this->m_as_channel <= AUDIO_ZONE_COUNT))
	{
		bytes_read = fread(pVa, 1,
				(pAudioSrc->bufByteUsed < remainder ?
						pAudioSrc->bufByteUsed : remainder), pAudioSrc->pFile);

		pAudioSrc->dataReaded += bytes_read;
		return bytes_read;
	}

	int32_t sampleBytes = pAudioSrc->sampleBits / 8;  // 3bit  单同道3bit  立体声(左右)6bit  当一块 BlockA

//	LOGINF("m_as_channel: %d, sampleBytes:%d  dualBlockAlign:%d ", this->m_as_channel, sampleBytes, pAudioSrc->sampleBlockAlign * 2);
//	LOGINF("bufByteUsed: %d, bufByteRead:%d, sampleChannels:%d", pAudioSrc->bufByteUsed, pAudioSrc->bufByteRead, pAudioSrc->sampleChannels);

	if (NUM_CHANNELS_MONO == pAudioSrc->sampleChannels)  // 单声道
	{
		int32_t dualBlockAlign = pAudioSrc->sampleBlockAlign * 2; // 单同道3*2bit  立体声(左右)6*2=12

		uint8_t origArr[pAudioSrc->bufByteRead];
		memset(origArr, 0x0, pAudioSrc->bufByteRead);

		bytes_read = fread(origArr, 1,
				(pAudioSrc->bufByteRead < remainder ? pAudioSrc->bufByteRead : remainder), pAudioSrc->pFile);

		int bufsize = pAudioSrc->bufByteUsed;
		if(bytes_read < pAudioSrc->bufByteRead)
		{
			bufsize = bytes_read*4;
		}

		uint8_t resArr[bufsize];  // 申请4通道数据大小， pAudioSrc->bufByteUsed 大小详情见initfile 赋值
		memset(resArr, 0x0, bufsize);
		//LOGINF("bytes_read(%d), bufsize(%d)", bytes_read, bufsize);

		switch (this->m_as_channel)
		{
			case CHANNEL_STEREO_FL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_FR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_RL_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FLFR_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FLFR_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_RLRR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR_RLRR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			default: //CHANNEL_STEREO_FLFR_RLRR
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i],sampleBytes);
					i += sampleBytes; // 单通道  =3
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
		}

		pAudioSrc->dataReaded += bytes_read;

		//memscpy(pVa, &resArr[0], pAudioSrc->bufByteUsed);
		memscpy(pVa, &resArr[0], bufsize);

		bytes_read *= 4;

		if(4 != channel) // 限制ref参考音 不录制 // 48000 //24bit
		{
//			if( pfile != NULL)
//			{
//				fwrite (&resArr[0], 1, bufsize, pfile);
//			}
		}
	}
	else if(NUM_CHANNELS_STEREO == pAudioSrc->sampleChannels)
	{
		int32_t dualBlockAlign = pAudioSrc->sampleBlockAlign; // 单同道3*2bit  立体声(左右)6*2=12

		uint8_t origArr[pAudioSrc->bufByteRead];
		memset(origArr, 0x0, pAudioSrc->bufByteRead);

		bytes_read = fread(origArr, 1,
				(pAudioSrc->bufByteRead < remainder ? pAudioSrc->bufByteRead : remainder), pAudioSrc->pFile);

		uint8_t resArr[pAudioSrc->bufByteUsed];
		memset(resArr, 0x0, pAudioSrc->bufByteUsed);

		// 如果是立体声(双声道),需要将数据改为4声道
		switch (this->m_as_channel)
		{
			case CHANNEL_STEREO_FL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_FR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_RL_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FLFR_RR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FLFR_RL:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FL_RLRR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			case CHANNEL_STEREO_FR_RLRR:
			{
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
			}
			default: //CHANNEL_STEREO_FLFR_RLRR
				for (uint32_t i = 0,j = 0; i < bytes_read;)
				{
					memscpy(&resArr[j], &origArr[i],sampleBytes);
					memscpy(&resArr[j+sampleBytes], &origArr[i+sampleBytes],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*2)], &origArr[i],sampleBytes);
					memscpy(&resArr[j+(sampleBytes*3)], &origArr[i+sampleBytes],sampleBytes);
					i += sampleBytes*2;  // 双声道(左右) 3*2 = 6;
					j += dualBlockAlign*2; // 四同步偏移12
				}
				break;
		}

		pAudioSrc->dataReaded += bytes_read;

		memscpy(pVa, &resArr[0], pAudioSrc->bufByteUsed);
		bytes_read = 2*bytes_read;

		if(4 != channel)  // 限制ref参考音 不录制
		{
//			if( pfile != NULL)
//			{
//				fwrite (&resArr[0], 1, bytes_read, pfile);
//			}
		}
	}
	else
	{
		bytes_read = fread(pVa, 1,
				(pAudioSrc->bufByteUsed < remainder ?
						pAudioSrc->bufByteUsed : remainder), pAudioSrc->pFile);

		pAudioSrc->dataReaded += bytes_read;
	}

	return bytes_read;
}

int32_t CsdOut::teardownCsdSession(int audio_channel) {
	int rc = CSD_EOK;

	/* Wait for EOS event. */
	pthread_mutex_lock(&m_csd_mutex);
	{
		if ((m_csd_event_type & CSD_EOS_EVENT)) {
			m_csd_event_type &= ~CSD_EOS_EVENT;
		}
		else
		{
			struct timespec ts;
			int32_t iRet = clock_gettime( CLOCK_MONOTONIC, &ts);
			if ( iRet != 0 )
			{
				LOGERR( "clock_gettime failed: %s.", strerror( errno ) );
			}
			else
			{
				int32_t uiSeconds = ( 500 / (int32_t)1000 );
				int32_t uiNanoSeconds = ( (500 % (int32_t)1000) * 1000000 );
				uiNanoSeconds += ts.tv_nsec;
				ts.tv_nsec = ( uiNanoSeconds % 1000000000 );
				uiSeconds += ( uiNanoSeconds / 1000000000 );
				ts.tv_sec += uiSeconds;
			}

			while (1)
			{
				LOGINF("(%d)teardownCsdSession => Staring Waiting for EOS", audio_channel);
				int ret = pthread_cond_timedwait(&m_csd_cond, &m_csd_mutex,&ts);  // 增加wait超时时间 500ms
				if(ret == ETIMEDOUT)
				{
					fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
							AUDIOSERVICE_PARAM_51);
					m_csd_event_type |= CSD_EOS_EVENT;
					LOGERR("EOS over time...");
				}
				//pthread_cond_wait(&m_csd_cond, &m_csd_mutex);  // 一直等待
				if ((m_csd_event_type & CSD_EOS_EVENT))
				{
					m_csd_event_type &= ~CSD_EOS_EVENT;
					LOGINF("(%d)teardownCsdSession => EOS event received",audio_channel);
					break;
				}
			}
		}
	}
	pthread_mutex_unlock(&m_csd_mutex);

	LOGINF("(%d)Play Audio Stream End[%llums]", audio_channel,getTickms());

	this->setStopSession(m_as_handle);

	freeCsdBufferPool(m_as_handle, m_buffer_pool,
			m_buffer_pool_count, m_buffer_pool_buf_size);

	this->streamDetach(m_ac_handle, m_as_handle);

	this->closeStream(m_as_handle);
	m_as_handle = 0;

	pthread_mutex_lock(&m_csd_mutex);
	m_csd_event_type = 0;
	pthread_mutex_unlock(&m_csd_mutex);

	return rc;
}

uint32_t CsdOut::openApm(void) {
	uint32_t handle = 0;

	handle = csd_open(CSD_OPEN_AUDIO_POLICY_MANAGER, NULL, 0);
	if (0 == handle) {
		LOGERR("csd_open(CSD_OPEN_AUDIO_POLICY_MANAGER) failed\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_2);
	}

	return handle;
}

uint32_t CsdOut::openDevice(void) {
	uint32_t handle = 0;

	handle = csd_open(CSD_OPEN_DEVICE_CONTROL, NULL, 0);
	if (0 == handle)
	{
		LOGERR("csd_open(CSD_OPEN_DEVICE_CONTROL) failed\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_3);
	}

	return handle;
}

uint32_t CsdOut::openAudioStreamV2(uint32_t buf_mem_type, uint32_t bit_width,
		uint32_t format_type, uint32_t open_mask) {
	uint32_t handle = 0;

	struct csd_as_open_v2 as_open_param_v2;
	memset((void *) &as_open_param_v2, 0, sizeof(struct csd_as_open_v2));

	as_open_param_v2.op_code = CSD_OPEN_OP_W;
	as_open_param_v2.buf_mem_type = buf_mem_type;
	as_open_param_v2.data_mode = CSD_AS_DATA_MODE_ASYNC;
	as_open_param_v2.open_mask = open_mask;
	as_open_param_v2.session_id = 0;
	as_open_param_v2.bit_width = bit_width;

	as_open_param_v2.format_type_rx = format_type;

	handle = csd_open(CSD_OPEN_AUDIO_STREAM_V2, (void *) &as_open_param_v2,
			sizeof(struct csd_as_open_v2));
	if (0 == handle) {
		LOGERR("csd_open(CSD_OPEN_AUDIO_STREAM_V2) failed\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_6);
	}

	return handle;
}

uint32_t CsdOut::openAudioContext(uint32_t dev_id, uint32_t sample_rate) {
	uint32_t handle = 0;

	struct csd_ac_open ac_open_param;
	memset((void *) &ac_open_param, 0, sizeof(struct csd_ac_open));

	ac_open_param.sample_rate = sample_rate;
	ac_open_param.ac_category = CSD_AC_CATEGORY_GENERIC_PLAYBACK;
	ac_open_param.dev_id = dev_id;
	ac_open_param.ac_mode = CSD_AC_MODE_LIVE;

	handle = csd_open(CSD_OPEN_AUDIO_CONTEXT, (void *) &ac_open_param,
			sizeof(struct csd_ac_open));
	if (0 == handle) {
		LOGERR("csd_open(CSD_OPEN_AUDIO_CONTEXT) failed(dev_id=%d)\r\n",
				dev_id);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_8);
	}

	return handle;
}

uint32_t CsdOut::openAudioContextV2(uint32_t dev_id, uint32_t sample_rate,
		uint32_t sample_width, uint32_t open_mask, uint32_t ac_category) {
	uint32_t ac_handle;

	struct csd_ac_open_v2 ac_open_param_v2;
	memset((void *) &ac_open_param_v2, 0, sizeof(struct csd_ac_open_v2));

	ac_open_param_v2.sample_rate = sample_rate;
	ac_open_param_v2.ac_category = ac_category;
	ac_open_param_v2.dev_id = dev_id;
	ac_open_param_v2.ac_mode = CSD_AC_MODE_LIVE;
	ac_open_param_v2.open_mask = open_mask;
	ac_open_param_v2.bit_width = sample_width;

	ac_handle = csd_open(CSD_OPEN_AUDIO_CONTEXT_V2, (void *) &ac_open_param_v2,
			sizeof(struct csd_ac_open_v2));
	if (0 == ac_handle) {
		LOGDBG("csd_open(CSD_OPEN_AUDIO_CONTEXT_V2) failed(dev_id=%d)\r\n",
				dev_id);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_9);
	}

	return ac_handle;
}

int32_t CsdOut::streamAttach(uint32_t ac_handle, uint32_t as_handle) {
	int32_t rc = CSD_EOK;

	struct csd_ac_as_attach ac_as_attach_param;
	memset((void *) &ac_as_attach_param, 0, sizeof(struct csd_ac_as_attach));

	ac_as_attach_param.num_as_handles = 1;
	ac_as_attach_param.as_handles = &as_handle;

	rc = csd_ioctl(ac_handle, CSD_AC_CMD_AS_ATTACH,
			(void *) &ac_as_attach_param, sizeof(struct csd_ac_as_attach));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AC_CMD_AS_ATTACH) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_10);
	}

	return rc;
}

int32_t CsdOut::enableContext(uint32_t ac_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_ioctl(ac_handle, CSD_AC_CMD_ENABLE, NULL, 0);
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AC_CMD_ENABLE) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_11);
	}

	return rc;
}

int32_t CsdOut::enableDevice(uint32_t dev_id, uint32_t sample_rate,
		uint32_t sample_width, uint32_t dev_handle) {
	int32_t rc = CSD_EOK;

	struct csd_dev_enable dev_param;
	memset((void *) &dev_param, 0, sizeof(struct csd_dev_enable));

	dev_param.num_devs = 1;

	dev_param.devs[0].dev_id = dev_id;
	dev_param.devs[0].dev_attrib.sample_rate = sample_rate;
	dev_param.devs[0].dev_attrib.bits_per_sample = sample_width;

	rc = csd_ioctl(dev_handle, CSD_DEV_CMD_ENABLE, (void *) &dev_param,
			sizeof(struct csd_dev_enable));
	if ((CSD_EOK == rc) || (CSD_EALREADY == rc)) {
		rc = CSD_EOK;
	} else {
		LOGERR("csd_ioctl(CSD_DEV_CMD_ENABLE) failed: 0x%x (dev_id=%d)\r\n", rc,
				dev_id);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_12);
	}

	return rc;
}

int32_t CsdOut::setDevVolumeMaster(uint32_t dev_handle, uint32_t dev_id,
		uint32_t dev_volume) {
	int32_t rc = CSD_EOK;
	csd_dev_pp_config_t dev_pp = { 0 };

	dev_pp.dev_id = dev_id;
	dev_pp.pp_type = CSD_PP_TYPE_VOL_V2;
	dev_pp.u.vol.param_id = CSD_PP_VOL_PARAM_ID_MASTER_GAIN;
	dev_pp.u.vol.u.master_vol.is_linear = 1;
	dev_pp.u.vol.u.master_vol.vol.value = dev_volume;

	rc = csd_ioctl(dev_handle, CSD_DEV_CODEC_CMD_SET_PP_CONFIG, &dev_pp,
			sizeof(dev_pp));
	if (CSD_EOK != rc) {
		LOGERR(
				"csd_ioctl(CSD_DEV_CODEC_CMD_SET_PP_CONFIG) failed: 0x%x (dev_id=%d)\r\n",
				rc, dev_id);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_13);
	}

	return rc;
}

int32_t CsdOut::setAsVolumeMaster(uint32_t as_handle, uint32_t vol) {
	int32_t rc = CSD_EOK;

	struct csd_as_pp_config as_pp_t;

	struct csd_aud_pp_vol *pVol = &as_pp_t.u.vol;
	memset(pVol, 0, sizeof(*pVol));

	as_pp_t.pp_type = CSD_AUD_PP_TYPE_VOL;

	pVol->param_id = CSD_AUD_PP_VOL_PARAM_ID_MASTER_GAIN;
	pVol->u.master_gain.master_gain_step = vol;
	pVol->u.master_gain.reserved = 0;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_CONFIG_PP_PARAMS, &as_pp_t,
			sizeof(as_pp_t));
	if (CSD_EOK != rc) {
		LOGERR(
				"csd_ioctl(CSD_AS_CMD_CONFIG_PP_PARAMS, CSD_AUD_PP_VOL_PARAM_ID_MASTER_GAIN) failed: 0x%x\r\n",
				rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_14);
	}

	return rc;
}

int32_t CsdOut::setAsMute(uint32_t as_handle, uint32_t mute) {
	int32_t rc = CSD_EOK;

	struct csd_as_pp_config as_pp_t;

	struct csd_aud_pp_vol *pVol = &as_pp_t.u.vol;
	memset(pVol, 0, sizeof(*pVol));

	as_pp_t.pp_type = CSD_AUD_PP_TYPE_VOL;

	pVol->param_id = CSD_AUD_PP_VOL_PARAM_ID_MUTE;
	pVol->u.mute.mute = mute;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_CONFIG_PP_PARAMS, &as_pp_t,
			sizeof(as_pp_t));
	if (CSD_EOK != rc) {
		LOGERR(
				"csd_ioctl(CSD_AS_CMD_CONFIG_PP_PARAMS, CSD_AUD_PP_VOL_PARAM_ID_MUTE) failed: 0x%x\r\n",
				rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_15);
	}

	return rc;
}

int32_t CsdOut::setStreamFormat(uint32_t as_handle) {
	int32_t rc = CSD_EOK;
	struct csd_as_fmt_cfg as_fmt_cfg_param;

	memset((void *) &as_fmt_cfg_param, 0, sizeof(struct csd_as_fmt_cfg));

	struct csd_as_fmt_rx_pcm *pcm_format = &as_fmt_cfg_param.rx_fmt.u.pcm;
	as_fmt_cfg_param.rx_fmt.fmt_type = CSD_FORMAT_PCM;
	as_fmt_cfg_param.rx_fmt.fmt_size = sizeof(struct csd_as_fmt_rx_pcm);

	pcm_format->sample_rate = 48000;
	pcm_format->channels = CSD_AS_FMT_CHANNEL_MODE_MONO;
	pcm_format->bit_per_sample = 16;
	pcm_format->sign_flag = 1;
	pcm_format->interleave_flag = 1;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_SET_STREAM_FMT,
			(void *) &as_fmt_cfg_param, sizeof(struct csd_as_fmt_cfg));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_SET_STREAM_FMT) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_16);
	}

	return rc;
}

int32_t CsdOut::setStreamFormat2(uint32_t as_handle, uint32_t sample_rate,
		uint16_t channels, uint16_t bit_per_sample, uint16_t sign_flag,
		uint16_t interleave_flag) {
	int32_t rc = CSD_EOK;

	struct csd_as_fmt_cfg as_fmt_cfg_param;
	memset((void *) &as_fmt_cfg_param, 0, sizeof(struct csd_as_fmt_cfg));

	struct csd_as_fmt_rx_pcm *pcm_format_rx = &as_fmt_cfg_param.rx_fmt.u.pcm;
	as_fmt_cfg_param.rx_fmt.fmt_type = CSD_FORMAT_PCM;
	as_fmt_cfg_param.rx_fmt.fmt_size = sizeof(struct csd_as_fmt_rx_pcm);

	pcm_format_rx->sample_rate = sample_rate;
	pcm_format_rx->channels = channels;
	pcm_format_rx->bit_per_sample = bit_per_sample;
	pcm_format_rx->sign_flag = sign_flag;
	pcm_format_rx->interleave_flag = interleave_flag;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_SET_STREAM_FMT,
			(void *) &as_fmt_cfg_param, sizeof(struct csd_as_fmt_cfg));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_SET_STREAM_FMT) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_17);
	}

	return rc;
}

int32_t CsdOut::setStreamFormatPcmMultichannel(uint32_t as_handle,
		uint32_t sample_rate, uint16_t num_channels, uint16_t bits_per_sample,
		uint16_t sign_flag, uint16_t interleave_flag,
		uint8_t *channel_mapping) {
	int32_t rc = CSD_EOK;

	struct csd_as_fmt_cfg as_fmt_cfg_param;

	struct csd_as_fmt_rx_multi_channel_pcm *mpcm_format_rx = &as_fmt_cfg_param.rx_fmt.u.multi_channel_pcm;

	memset((void *) &as_fmt_cfg_param, 0, sizeof(struct csd_as_fmt_cfg));

	as_fmt_cfg_param.rx_fmt.fmt_type = CSD_FORMAT_MULTI_CHANNEL_PCM;
	as_fmt_cfg_param.rx_fmt.fmt_size =
			sizeof(struct csd_as_fmt_rx_multi_channel_pcm);
	mpcm_format_rx->sample_rate = sample_rate;
	mpcm_format_rx->sign_flag = sign_flag;
	mpcm_format_rx->interleave_flag = interleave_flag;
	mpcm_format_rx->num_channels = num_channels;
	mpcm_format_rx->bits_per_sample = bits_per_sample;

	memset(&mpcm_format_rx->channel_mapping[0], 0x0,
			sizeof(mpcm_format_rx->channel_mapping[0]));

	int n = (num_channels > MAX_NUM_CHANNELS) ?
			(MAX_NUM_CHANNELS) : (num_channels);
	for (int i = 0; i < n; i++) {
		mpcm_format_rx->channel_mapping[i] = channel_mapping[i];
	}

	rc = csd_ioctl(as_handle, CSD_AS_CMD_SET_STREAM_FMT,
			(void *) &as_fmt_cfg_param, sizeof(struct csd_as_fmt_cfg));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_SET_STREAM_FMT) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_18);
	}

	return rc;
}

int32_t CsdOut::setStreamFormatPcmMultichannelV2(uint32_t as_handle,
		uint32_t sample_rate, uint16_t num_channels, uint16_t bits_per_sample,
		uint16_t sign_flag, uint16_t interleave_flag, uint16_t sample_word_size,
		uint16_t endianness, uint16_t mode, uint8_t *channel_mapping) {
	int32_t rc = CSD_EOK;

	struct csd_as_fmt_cfg as_fmt_cfg_param;
	struct csd_as_fmt_rx_multi_channel_pcm_v2 *mpcm_format_rx =
			&as_fmt_cfg_param.rx_fmt.u.multi_channel_pcm_v2;

	memset((void *) &as_fmt_cfg_param, 0, sizeof(struct csd_as_fmt_cfg));

	as_fmt_cfg_param.rx_fmt.fmt_type = CSD_FORMAT_MULTI_CHANNEL_PCM_V2;
	as_fmt_cfg_param.rx_fmt.fmt_size =
			sizeof(struct csd_as_fmt_rx_multi_channel_pcm_v2);
	mpcm_format_rx->sample_rate = sample_rate;
	mpcm_format_rx->sign_flag = sign_flag;
	mpcm_format_rx->interleave_flag = interleave_flag;
	mpcm_format_rx->num_channels = num_channels;
	mpcm_format_rx->bits_per_sample = bits_per_sample;
	mpcm_format_rx->sample_word_size = sample_word_size;
	mpcm_format_rx->endianness = endianness;
	mpcm_format_rx->mode = mode;

	memset(&mpcm_format_rx->channel_mapping[0], 0x0,
			sizeof(mpcm_format_rx->channel_mapping[0]));

	int n = (num_channels > MAX_NUM_CHANNELS_V2) ?
			(MAX_NUM_CHANNELS_V2) : (num_channels);
	for (int i = 0; i < n; i++) {
		mpcm_format_rx->channel_mapping[i] = channel_mapping[i];
	}

	rc = csd_ioctl(as_handle, CSD_AS_CMD_SET_STREAM_FMT,
			(void *) &as_fmt_cfg_param, sizeof(struct csd_as_fmt_cfg));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_SET_STREAM_FMT) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_19);
	}

	return rc;
}

int32_t CsdOut::setStreamContextMultichannel(uint32_t as_handle,
		uint32_t ac_handle, uint16_t num_channels, uint8_t *channel_mapping) {
	int32_t rc = CSD_EOK;

	struct csd_as_stream_config_multi_channel as_multi_ch;
	struct csd_ac_config_multi_channel ac_multi_ch;

	memset((void *) &as_multi_ch, 0, sizeof(as_multi_ch));
	as_multi_ch.num_channels = num_channels;
	as_multi_ch.channel_mapping = channel_mapping;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_CONFIG_DECODER_MULTI_CHANNEL,
			(void *) &as_multi_ch, sizeof(as_multi_ch));
	if (CSD_EOK != rc)
	{
		LOGERR("csd_ioctl(CSD_AS_CMD_CONFIG_DECODER_MULTI_CHANNEL) failed: 0x%x\r\n",rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_20);
		return rc;
	}

	memset((void *) &ac_multi_ch, 0, sizeof(ac_multi_ch));
	ac_multi_ch.num_channels = num_channels;
	ac_multi_ch.channel_mapping = channel_mapping;

	rc = csd_ioctl(ac_handle, CSD_AC_CMD_CONFIG_MULTI_CHANNEL, (void *) &ac_multi_ch, sizeof(ac_multi_ch));
	if (CSD_EOK != rc)
	{
		LOGERR("csd_ioctl(CSD_AC_CMD_CONFIG_MULTI_CHANNEL) failed: 0x%x\r\n",
				rc);

		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_21);
		return rc;
	}

	return rc;
}

int32_t CsdOut::setStartSession(uint32_t as_handle) {
	int32_t rc = CSD_EOK;
	struct csd_as_ts as_ts;

	as_ts.ts_type = CSD_AS_TS_UNKNOWN;
	as_ts.ts_high = 0;
	as_ts.ts_low = 0;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_START_SESSION, (void *) &as_ts,
			sizeof(struct csd_as_ts));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_START_SESSION) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_22);
	}

	return rc;
}

int32_t CsdOut::setStopSession(uint32_t as_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_STOP_SESSION, NULL, 0);
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_STOP_SESSION) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_23);
	}

	return rc;
}

int32_t CsdOut::dinitCsd() {
	int32_t rc = CSD_EOK;

	this->disableContext(m_ac_handle);
	this->disableDevice(m_dev_id, m_dev_handle);

	this->closeContext(m_ac_handle);
	this->closeDevice(m_dev_handle);

	m_ac_handle = 0;
	m_dev_handle = 0;

	rc = csd_deinit();
	if (CSD_EOK != rc) {
		LOGERR("csd_deinit failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_24);
	}

	return rc;
}

int32_t CsdOut::closeApm(uint32_t apm_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_close(apm_handle);
	if (CSD_EOK != rc) {
		LOGERR("csd_close(apm) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_25);
	}

	return rc;
}

int32_t CsdOut::apmRegisterClient(uint32_t apm_handle, csd_apm_cb_fn cb,
		void *data) {
	int32_t rc = CSD_EOK;

	struct csd_apm_cb registerCallBack;
	memset((void *) &registerCallBack, 0, sizeof(struct csd_apm_cb));

	registerCallBack.cb = cb;
	registerCallBack.data = data;

	rc = csd_ioctl(apm_handle, CSD_APM_CMD_REGISTER_CLIENT, &registerCallBack,
			sizeof(registerCallBack));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_APM_CMD_REGISTER_CLIENT) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_26);
	}

	return rc;
}

int32_t CsdOut::apmDeregisterClient(uint32_t apm_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_ioctl(apm_handle, CSD_APM_CMD_DEREGISTER_CLIENT, NULL, 0);
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_APM_CMD_DEREGISTER_CLIENT) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_27);
	}

	return rc;
}

int32_t CsdOut::apmRequestPermission(uint32_t apm_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_ioctl(apm_handle, CSD_APM_CMD_REQUEST_PERMISSION, NULL, 0);
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_APM_CMD_REQUEST_PERMISSION) failed: 0x%x\r\n",
				rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_40);
	}

	return rc;
}

int32_t CsdOut::apmReleasePermission(uint32_t apm_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_ioctl(apm_handle, CSD_APM_CMD_RELEASE_PERMISSION, NULL, 0);
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_APM_CMD_RELEASE_PERMISSION) failed: 0x%x\r\n",
				rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_41);
	}

	return rc;
}

int32_t CsdOut::disableContext(uint32_t ac_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_ioctl(ac_handle, CSD_AC_CMD_DISABLE, NULL, 0);
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AC_CMD_DISABLE) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_28);
	}

	return rc;
}

int32_t CsdOut::streamDetach(uint32_t ac_handle, uint32_t as_handle) {
	int32_t rc = CSD_EOK;

	struct csd_ac_as_detach ac_as_detach_param;
	memset((void *) &ac_as_detach_param, 0, sizeof(ac_as_detach_param));

	ac_as_detach_param.num_as_handles = 1;
	ac_as_detach_param.as_handles = &as_handle;

	rc = csd_ioctl(ac_handle, CSD_AC_CMD_AS_DETACH, &ac_as_detach_param,
			sizeof(struct csd_ac_as_detach));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AC_CMD_AS_DETACH) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_29);
	}

	return rc;
}

int32_t CsdOut::disableDevice(uint32_t dev_id, uint32_t dev_handle) {
	int32_t rc = CSD_EOK;

	struct csd_dev_disable dev_param;
	memset((void *) &dev_param, 0, sizeof(dev_param));
	dev_param.num_devs = 1;
	dev_param.dev_ids[0] = dev_id;

	rc = csd_ioctl(dev_handle, CSD_DEV_CMD_DISABLE, &dev_param,
			sizeof(struct csd_dev_disable));
	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AC_CMD_AS_DETACH) failed: 0x%x (dev_id=%d)\r\n",
				rc, dev_id);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_30);
	}

	return rc;
}

int32_t CsdOut::closeContext(uint32_t ac_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_close(ac_handle);
	if (CSD_EOK != rc) {
		LOGERR("csd_close(ac) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_31);
	}

	return rc;
}

int32_t CsdOut::closeStream(uint32_t as_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_close(as_handle);
	if (CSD_EOK != rc) {
		LOGERR("csd_close(as) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_32);
	}

	return rc;
}

int32_t CsdOut::closeDevice(uint32_t dev_handle) {
	int32_t rc = CSD_EOK;

	rc = csd_close(dev_handle);
	if (CSD_EOK != rc) {
		LOGERR("csd_close(dev) failed: 0x%x\r\n", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_33);
	}

	return rc;
}

int32_t CsdOut::allocateCsdBufferPool(uint32_t as_handle,
		buffer_pool_t *buffer_pool, uint32_t buffer_count,
		uint32_t buffer_size) {
	int32_t rc = CSD_EOK;

	uint32_t i = 0;
	uintptr_t pa_client = 0;
	size_t contig_len = 0;
	csd_as_pmem_info_t as_pmem_info = { 0 };
	csd_as_map_smem_t as_map_smem = { 0 };

	LOGDBG("allocateCsdBufferPool: ENTER (buffer_count=%d, buffer_size=%d)",
			buffer_count, buffer_size);

	if (NULL == buffer_pool || 0 == buffer_count || 0 == buffer_size) {
		rc = CSD_EBADPARAM;
		return rc;
	}

	memset(buffer_pool, 0x0, sizeof(*buffer_pool) * buffer_count);
	for (i = 0; i < buffer_count; i++) {
		/* Request pmem allocation from CSD */
		memset(&as_pmem_info, 0x0, sizeof(as_pmem_info));
		as_pmem_info.nSize = buffer_size;
		as_pmem_info.nPid = getpid();
		as_pmem_info.mem_type = 0;
		as_pmem_info.va = NULL;

		rc = csd_ioctl(as_handle, CSD_AS_CMD_ALLOC_PMEM, (void *) &as_pmem_info,
				sizeof(as_pmem_info));
		if (CSD_EOK != rc) {
			LOGERR(
					"allocateCsdBufferPool: CSD_AS_CMD_ALLOC_PMEM failed on i=%d!",
					i);
			fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
					AUDIOSERVICE_PARAM_34);
			rc = CSD_ENORESOURCE;
			return rc;
		}

		mem_offset64(as_pmem_info.va, NOFD, buffer_size, (off64_t *) &pa_client,
				&contig_len);

		buffer_pool[i].pVa = as_pmem_info.va;
		buffer_pool[i].pPa = (uint8_t *) pa_client;
		buffer_pool[i].bAllocated = TRUE;

		/* Request memory to be mapped */
		memset(&as_map_smem, 0x0, sizeof(as_map_smem));
		as_map_smem.mem_size = buffer_size;
		as_map_smem.mem_addr = (uint8_t *) buffer_pool[i].pPa;
		as_map_smem.mem_type = CSD_AS_SMEM_EBI;

		rc = csd_ioctl(as_handle, CSD_AS_CMD_MAP_SHARED_MEMORY, &as_map_smem,
				sizeof(as_map_smem));
		if (CSD_EOK != rc) {
			LOGERR(
					"allocateCsdBufferPool: CSD_AS_CMD_MAP_SHARED_MEMORY failed on i=%d!",
					i);
			fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
					AUDIOSERVICE_PARAM_35);
			rc = CSD_EFAILED;
			return rc;
		}
	}

	return rc;
}

int32_t CsdOut::freeCsdBufferPool(uint32_t as_handle,
		buffer_pool_t *buffer_pool, uint32_t buffer_count,
		uint32_t buffer_size) {
	int32_t rc = CSD_EOK;

	uint32_t i = 0;
	csd_as_pmem_info_t as_pmem_info = { 0 };
	csd_as_unmap_smem_t as_unmap_smem = { 0 };

	LOGDBG("freeCsdBufferPool: ENTER (buffer_count=%d, buffer_size=%d)",
			buffer_count, buffer_size);

	for (i = 0; i < buffer_count; ++i) {
		/* Request memory to be un-mapped */
		memset(&as_unmap_smem, 0x0, sizeof(as_unmap_smem));
		as_unmap_smem.mem_addr = buffer_pool[i].pPa;

		rc = csd_ioctl(as_handle, CSD_AS_CMD_UNMAP_SHARED_MEMORY,
				&as_unmap_smem, sizeof(as_unmap_smem));
		if (CSD_EOK != rc) {
			LOGERR(
					"freeCsdBufferPool: CSD_AS_CMD_UNMAP_SHARED_MEMORY failed on i=%d!",
					i);
			fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
					AUDIOSERVICE_PARAM_36);
			rc = CSD_EFAILED;
			return rc;
		}

		/* Request pmem free from CSD */
		memset(&as_pmem_info, 0x0, sizeof(as_pmem_info));
		as_pmem_info.nSize = buffer_size;
		as_pmem_info.nPid = getpid();
		as_pmem_info.va = buffer_pool[i].pVa;

		rc = csd_ioctl(as_handle, CSD_AS_CMD_FREE_PMEM, (void *) &as_pmem_info,
				sizeof(as_pmem_info));
		if (CSD_EOK != rc) {
			LOGERR("freeCsdBufferPool: CSD_AS_CMD_FREE_PMEM failed on i=%d!",
					i);
			fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
					AUDIOSERVICE_PARAM_37);
			rc = CSD_ENORESOURCE;
			return rc;
		}
	}

	memset(buffer_pool, 0x0, sizeof(*buffer_pool) * buffer_count);

	return rc;
}

int32_t CsdOut::reserveCsdBufferFromPool(buffer_pool_t *buffer,
		uint32_t buffer_count, uint32_t buffer_free_count, uint32_t *index) {
	int32_t rc = CSD_ENORESOURCE;
	uint32_t i = 0;

	for (i = 0; i < buffer_count; ++i) {
		if (TRUE == buffer[i].bAllocated && FALSE == buffer[i].bUsed) {
			*index = i;
			buffer[i].bUsed = TRUE;
			buffer_free_count--;
			rc = CSD_EOK;
			break;
		}
	}

	return rc;
}

int32_t CsdOut::releaseCsdBufferFromPool(buffer_pool_t *buffer,
		uint32_t buffer_count, uint32_t buffer_free_count, uint8_t *pBuf,
		uint32_t *token, uint32_t buf_size) {
	int32_t rc = CSD_ENOTFOUND;
	uint32_t i = 0;

	// Match is attempted first with the full address in 'pBuf'.  If pBuf is
	// not given (NULL) then the token will be used to attempt a match on the
	// lower 32-bits of the buffer's PA or VA.
	if (pBuf) {
		for (i = 0; i < buffer_count; i++) {
			if (pBuf == buffer[i].pPa || pBuf == buffer[i].pVa) {
				buffer[i].bUsed = FALSE;
				buffer[i].nCsdBufSize = buf_size;
				buffer_free_count++;
				rc = CSD_EOK;
				break;
			}
		}
	} else if (token) {
		for (i = 0; i < buffer_count; ++i) {
			if (*token == (uint32_t) (uintptr_t) buffer[i].pPa
					|| *token == (uint32_t) (uintptr_t) buffer[i].pVa) {
				buffer[i].bUsed = FALSE;
				buffer[i].nCsdBufSize = buf_size;
				buffer_free_count++;
				rc = CSD_EOK;
				break;
			}
		}
	}

	return rc;
}

int32_t CsdOut::registerCallBack(uint32_t as_handle, void *cb, void *cb_data) {
	int32_t rc = CSD_EOK;
	struct csd_as_cb registerCallBack;

	memset((void *) &registerCallBack, 0, sizeof(struct csd_as_cb));
	registerCallBack.cb = (csd_as_cb_fn) cb;
	registerCallBack.cb_data = cb_data;

	rc = csd_ioctl(as_handle, CSD_AS_CMD_SET_EVT_CB, &registerCallBack,
			sizeof(registerCallBack));

	if (CSD_EOK != rc) {
		LOGERR("csd_ioctl(CSD_AS_CMD_SET_EVT_CB) failed: 0x%x", rc);
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_36);
	}

	return rc;
}

void CsdOut::csdUserCallBack(uint32_t evntId, void *pPayload,
		uint32_t payloadSize, void *pClientData) {
	CsdOut * pThis = static_cast<CsdOut*>(pClientData);
	if (NULL == pThis) {
		LOGERR("pThis cast to CsdOut is NULL\r\n");
		fyassert(AUDIOSERVICE_MD, AUDIOSERVICE_CSD_FAIL, LEVEL_WARNING,
				AUDIOSERVICE_PARAM_39);
		return;
	}

	switch (evntId) {
	case CSD_AS_EVT_BUF_DONE: {
		uint32_t *token = (uint32_t *) pPayload;
		/*LOGINF(
				"csdUserCallBack: Received CSD_AS_EVT_BUF_DONE event, token 0x%x",
				*token);*/

		CsdOut::releaseCsdBufferFromPool(pThis->m_buffer_pool,
				pThis->m_buffer_pool_count, pThis->m_buffer_pool_free_count,
				NULL, token, 0);

		pthread_mutex_lock(&pThis->m_csd_mutex);
		{
			pThis->m_csd_event_type |= CSD_BUF_DONE_EVENT;
			pthread_cond_signal(&pThis->m_csd_cond);
		}
		pthread_mutex_unlock(&pThis->m_csd_mutex);
		break;
	}
	case CSD_AS_EVT_BUF_READY: {
		//LOGINF("(%d)csdUserCallBack: Received CSD_AS_EVT_BUF_READY event", pThis->mychannel);
		pthread_mutex_lock(&pThis->m_csd_mutex);
		{
			pThis->m_csd_event_type |= CSD_BUF_READY_EVENT;
			pthread_cond_signal(&pThis->m_csd_cond);
		}
		pthread_mutex_unlock(&pThis->m_csd_mutex);
		break;
	}
	case CSD_AS_EVT_EOS: {
		//LOGINF("(%d)csdUserCallBack: Received CSD_AS_EVT_EOS event", pThis->mychannel);
		pthread_mutex_lock(&pThis->m_csd_mutex);
		{
			pThis->m_csd_event_type |= CSD_EOS_EVENT;
			pthread_cond_signal(&pThis->m_csd_cond);
		}
		pthread_mutex_unlock(&pThis->m_csd_mutex);
		break;
	}
	default: {
		//LOGDBG("csdUserCallBack: Received un-handled event = 0x%x", evntId);
		break;
	}
	}
}


int32_t CsdOut::AudioAsMute(uint32_t mute)
{
	LOGINF("CsdOut::AudioMute: %d", mute);
	int32_t rc = CSD_EOK;
	rc = this->setAsMute(this->m_as_handle, mute);
	if (CSD_EOK != rc) {
		LOGERR("setAsMute failed:");
	}
	return rc;
}

int CsdOut::AudioMp3ToWAV(const CHAR *inFileName, const CHAR *outFileName)
{
	LOGINF("AudioMp3ToWAV-----------------start");
	LOGINF("inFileName: %s", inFileName);
	LOGINF("outFileName: %s", outFileName);
	int ret = 0;
	if(NULL == inFileName || NULL == outFileName)
	{
		LOGERR("NULL == inFileName || NULL == outFileName");
		return -1;
	}

	sDecodeTime = getTimestamp();

	   // 打开输入文件
   AVFormatContext* formatCtx = avformat_alloc_context();
   if (avformat_open_input(&formatCtx, inFileName, nullptr, nullptr) != 0)
   {
       LOGERR("open url: %s failed.", inFileName);
       return -1;
   }

	// 获取音频信息
	if (avformat_find_stream_info(formatCtx, nullptr) < 0)
	{
		LOGERR("get audio stream info failed.");
		return -1;
	}

	int audioStreamIndex = 0;

	// 音频解码，需要找到对应的AVStream所在的pFormatCtx->streams的索引位置
	for (unsigned int i = 0; i < formatCtx->nb_streams; i++)
	{
		// 根据类型判断是否是音频流
		if (formatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audioStreamIndex = i;
			break;
		}
	}

	if (audioStreamIndex == -1)
    {
       LOGERR("get audio stream info failed.");
       avformat_close_input(&formatCtx);
       return -1;
    }

	// 获取音频流信息
   AVCodecParameters* codecParameters = formatCtx->streams[audioStreamIndex]->codecpar;
	// 获取解码器
	const AVCodec *codec = avcodec_find_decoder(codecParameters->codec_id);
	if (codec == nullptr)
	{
		LOGERR("get decoder failed!");
		return -1;
	}

	// 打开解码器
	AVCodecContext *codeCtx = avcodec_alloc_context3(codec);
	//用流解码信息初始化编码参数
	avcodec_parameters_to_context(codeCtx, codecParameters);
	codeCtx->pkt_timebase = formatCtx->streams[audioStreamIndex]->time_base;
	if (avcodec_open2(codeCtx, codec, nullptr) < 0)
	{
		LOGERR("open decoded failed!");
		return -1;
	}

	AVPacket *avPacket = av_packet_alloc();
	AVFrame *avFrame = av_frame_alloc();
	SwrContext *swrCtx = swr_alloc();

	// 重采样设置选项
	enum AVSampleFormat inSampleFmt = codeCtx->sample_fmt;
	enum AVSampleFormat outSampleFmt = AV_SAMPLE_FMT_S16;
	int SampleFmt = 16;
	// 获取输出的声道个数

	int num_channels = codeCtx->ch_layout.nb_channels;
	int inSampleRate = codeCtx->sample_rate;
	int outSampleRate = codeCtx->sample_rate;
    int bytes_per_sample = av_get_bytes_per_sample(outSampleFmt);
    const AVChannelLayout inChLayout = codeCtx->ch_layout;
    const AVChannelLayout outChLayout = codeCtx->ch_layout;

	//存储PCM数据
	int bufferSize = (num_channels * inSampleRate * 16) / 8.0;
	uint8_t *outBuffer = (uint8_t *)av_malloc(bufferSize / 2);

	LOGINF("num_channels = %d, bytes_per_sample: %d",num_channels, bytes_per_sample);
	LOGINF("inSampleFmt = %d, inSampleRate = %d, name = %s\n", inSampleFmt, inSampleRate, codec->name);

	swr_alloc_set_opts2(&swrCtx,
					&outChLayout, outSampleFmt, outSampleRate,
					&inChLayout, inSampleFmt, inSampleRate, 0, nullptr);
	swr_init(swrCtx);

	FILE* output_file = NULL;
	if (( output_file = fopen ("/fs/data/AudioService.wav", "wb")) == 0)
	{
		LOGERR("WAV_FILE_NAME open failed\n");
		return -1;
	}

	if(NULL == output_file)
	{
		LOGERR("error: NULL == output_file\n");
		return -1;
	}

	// Write WAV header
	fwrite("RIFF", 4, 1, output_file);
	int total_size = 0; // Set this value later
	fwrite(&total_size, 4, 1, output_file);
	fwrite("WAVE", 4, 1, output_file);
	fwrite("fmt ", 4, 1, output_file);
	int subchunk1_size = 16; // PCM format
	fwrite(&subchunk1_size, 4, 1, output_file);
	fwrite(&codeCtx->codec_id, 2, 1, output_file);
	fwrite(&num_channels, 2, 1, output_file);
	fwrite(&codeCtx->sample_rate, 4, 1, output_file);
	int byterate = codeCtx->sample_rate * bytes_per_sample * num_channels;
	fwrite(&byterate, 4, 1, output_file);
	int blockalign = num_channels * bytes_per_sample;
	fwrite(&blockalign, 2, 1, output_file);
	fwrite(&SampleFmt, 2, 1, output_file);
	fwrite("data", 4, 1, output_file);
	int subchunk2_size = 0; // Set this value later
	fwrite(&subchunk2_size, 4, 1, output_file);

	 LOGINF("codec_id:%d, channels: %d, sample_rate: %d",codeCtx->codec_id,num_channels, codeCtx->sample_rate);
	 LOGINF("byterate:%d, blockalign: %d, outSampleFmt: %d",byterate,blockalign, outSampleFmt);
	// 存储pcm数据 1秒的数据大小
	//int index = 0;
	av_seek_frame(formatCtx, audioStreamIndex, 0, AVSEEK_FLAG_BACKWARD);

	// 音频解码 裁剪前5s音频
//    int64_t start_time = av_rescale_q(audio_stream->start_time, audio_stream->time_base, AV_TIME_BASE_Q);
//    int64_t duration = av_rescale_q(audio_stream->duration, audio_stream->time_base, AV_TIME_BASE_Q);
//    int64_t end_time = start_time + duration;
//    LOGINF("start_time: %d, duration: %d, end_time: %d",start_time, duration, end_time);
	// 一帧一帧读取压缩的音频数据AVPacket
	while (av_read_frame(formatCtx, avPacket) >= 0)
	{
		//if (avPacket->stream_index == audioStreamIndex && avPacket->pts >= start_time && avPacket->pts <= end_time)
		if (avPacket->stream_index == audioStreamIndex)
		{
			if (avcodec_send_packet(codeCtx, avPacket) != 0)
			{
				LOGINF("avcodec_send_packet over\n");
				break;
			}

			while( avcodec_receive_frame(codeCtx, avFrame) == 0)
			{
				//LOGINF("decode frame:%d", index++);
				int dst_nb_samples = av_rescale_rnd(swr_get_delay(swrCtx, codeCtx->sample_rate) + avFrame->nb_samples,
					outSampleRate, codeCtx->sample_rate, AV_ROUND_UP);
				swr_convert(swrCtx, &outBuffer, dst_nb_samples, (const uint8_t **)avFrame->data, avFrame->nb_samples);
				// 获取sample的size
				int outSize = av_samples_get_buffer_size(nullptr, num_channels, avFrame->nb_samples,outSampleFmt, 1);

				if( output_file != NULL)
				{
					fwrite (outBuffer, 1, outSize, output_file);
					total_size += outSize;
				}
				// 更新解码包的信息
				avPacket->data = NULL;
				avPacket->size = 0;
			}
		}
		av_packet_unref(avPacket);
	}
	// chunkSize
	fseek(output_file, 0, SEEK_END);
    total_size = ftell(output_file) - 8;
    if(total_size > 0)
    {
		if (fseek(output_file, 4, SEEK_SET) == 0)
		{
			fwrite(&total_size, 4, 1, output_file);
		}
		else
		{
			LOGERR("fseek 4 failed\r\n");
			ret = -1;
		}
		// Subchunk2Size
		fseek(output_file, 0, SEEK_END);
		subchunk2_size = ftell(output_file) - 44;
		if(subchunk2_size > 0)
		{
			if (fseek(output_file, 40, SEEK_SET) == 0)
			{
				fwrite(&subchunk2_size, 4, 1, output_file);
			}
			else
			{
				LOGERR("fseek 40 failed\r\n");
				ret = -1;
			}
		}
		else
		{
			LOGERR("Error: ftell(output_file) - 44 < 0");
			subchunk2_size = inSampleRate * num_channels * SampleFmt/8;
			if (fseek(output_file, 40, SEEK_SET) == 0)
			{
				fwrite(&subchunk2_size, 4, 1, output_file);
			}
			else
			{
				LOGERR("fseek 40 failed\r\n");
				ret = -1;
			}
		}
		sync();
		LOGINF("total_size: %d, subchunk2_size: %d",total_size, subchunk2_size);
    }
    else
    {
    	LOGERR("Error: ftell(output_file) - 8 < 0");
    	subchunk2_size = inSampleRate * num_channels * SampleFmt/8;
    	total_size = 36 + subchunk2_size;

		if (fseek(output_file, 4, SEEK_SET) == 0)
		{
			fwrite(&total_size, 4, 1, output_file);
		}
		else
		{
			LOGERR("fseek 4 failed\r\n");
			ret = -1;
		}

		// Subchunk2Size
		if (fseek(output_file, 40, SEEK_SET) == 0)
		{
			fwrite(&subchunk2_size, 4, 1, output_file);
		}
		else
		{
			LOGERR("fseek 40 failed\r\n");
			ret = -1;
		}
		sync();
    }

	if( output_file != NULL)
	{
		fclose( output_file );
		output_file = NULL;
	}

	sDecodeTime = getTimestamp() - sDecodeTime;
	LOGINF("decode finished. total_size size: %d, use time: %lld ms",total_size, sDecodeTime);

	av_frame_free(&avFrame);
	swr_free(&swrCtx);
	avcodec_close(codeCtx);
	avformat_close_input(&formatCtx);
	LOGINF("AudioMp3ToWAV-----------------end");

	return ret;
}

