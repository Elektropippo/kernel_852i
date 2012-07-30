#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/jiffies.h>
#include <asm/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <asm/div64.h>

#include "rt5625.h"

#if REALTEK_HWDEP

#include <linux/ioctl.h>
#include <linux/types.h>

#endif

#if 0
#define DBG(x...) printk(KERN_INFO x)
#else
#define DBG(x...) do { } while (0)
#endif

#define AUDIO_NAME "rt5625"
#define RT5625_VERSION "0.03 alsa 1.0.21"
#define ALSA_SOC_VERSION "1.0.21"

#define RT5625_EQ_FUNC_ENA  0

static void hp_depop_mode2(struct snd_soc_codec *codec);
static void hp_mute_unmute_depop(struct snd_soc_codec *codec,int mute); 

struct rt5625_priv {
	unsigned int stereo_sysclk;
	unsigned int voice_sysclk;
};

struct rt5625_init_reg {
	u8 reg_index;
	u16 reg_value;	
};

static struct rt5625_init_reg rt5625_init_list[] = {

	{RT5625_HP_OUT_VOL			, 0x8888},	//default is -12db
	{RT5625_SPK_OUT_VOL 		, 0x8080},	//default is 0db
	{RT5625_DAC_AND_MIC_CTRL	, 0xee03},	//DAC to hpmixer
	{RT5625_OUTPUT_MIXER_CTRL	, 0x0748},	//all output from hpmixer
	{RT5625_MIC_CTRL			, 0x0500},	//mic boost 20db
	{RT5625_ADC_REC_MIXER		, 0x3f3f},	//record source from mic1
	{RT5625_GEN_CTRL_REG1		, 0x0c0a},	//speaker vdd ratio is 1
	{RT5625_ADC_REC_GAIN		, 0xd5d5},	//gain 15db of ADC by default

};

#define RT5625_INIT_REG_NUM ARRAY_SIZE(rt5625_init_list)

#if (RT5625_EQ_FUNC_ENA==1)
//*************************************************************************************************
//eq table
//*************************************************************************************************
enum
{
	NORMAL=0,
	CLUB,
	DANCE,
	LIVE,	
	POP,
	ROCK,
	OPPO,
	TREBLE,
	BASS	
};

typedef struct  _HW_EQ_PRESET
{
	u16 	HwEqType;
	u16 	EqValue[14];
	u16  HwEQCtrl;

}HW_EQ_PRESET;


HW_EQ_PRESET HwEq_Preset[]={
/*			0x0		0x1		0x2	  0x3	0x4		0x5		0x6		0x7	0x8		0x9		0xa	0xb		0xc		0x6e*/
	{NORMAL,{0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},0x0000},			
	{CLUB  ,{0x1C10,0x0000,0xC1CC,0x1E5D,0x0699,0xCD48,0x188D,0x0699,0xC3B6,0x1CD0,0x0699,0x0436,0x0000},0x800E},
	{DANCE ,{0x1F2C,0x095B,0xC071,0x1F95,0x0616,0xC96E,0x1B11,0xFC91,0xDCF2,0x1194,0xFAF2,0x0436,0x0000},0x800F},
	{LIVE  ,{0x1EB5,0xFCB6,0xC24A,0x1DF8,0x0E7C,0xC883,0x1C10,0x0699,0xDA41,0x1561,0x0295,0x0436,0x0000},0x800F},
	{POP   ,{0x1EB5,0xFCB6,0xC1D4,0x1E5D,0x0E23,0xD92E,0x16E6,0xFCB6,0x0000,0x0969,0xF988,0x0436,0x0000},0x800F},
	{ROCK  ,{0x1EB5,0xFCB6,0xC071,0x1F95,0x0424,0xC30A,0x1D27,0xF900,0x0C5D,0x0FC7,0x0E23,0x0436,0x0000},0x800F},
	{OPPO  ,{0x0000,0x0000,0xCA4A,0x17F8,0x0FEC,0xCA4A,0x17F8,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},0x800F},
	{TREBLE,{0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x188D,0x1699},0x8010},
	{BASS  ,{0x1A43,0x0C00,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000,0x0000},0x8001},
	
};

#endif
//*************************************************************************************************
//*************************************************************************************************

/*
 *	bit[0]  for linein playback switch
 *	bit[1] phone
 *	bit[2] mic1
 *	bit[3] mic2
 *	bit[4] vopcm
 *	
 */
#define HPL_MIXER 0x80
#define HPR_MIXER 0x82
static unsigned int reg80 = 0, reg82 = 0;

/*
 *	bit[0][1][2] use for aec control
 *  bit[3] for none  
 *	bit[4] for SPKL pga
 *	bit[5] for SPKR pga
 *	bit[6] for hpl pga
 *	bit[7] for hpr pga
 *  bit[8] for dump dsp
 *  bit[12~15] for eq function
 */
 #define VIRTUAL_REG_FOR_MISC_FUNC 0x84
static unsigned int reg84 = 0;


static const u16 rt5625_reg[] = {
	0x59b4, 0x8080, 0x8080, 0x8080,		/*reg00-reg06*/
	0xc800, 0xe808, 0x1010, 0x0808,		/*reg08-reg0e*/
	0xe0ef, 0xcbcb, 0x7f7f, 0x0000,		/*reg10-reg16*/
	0xe010, 0x0000, 0x8008, 0x2007,		/*reg18-reg1e*/
	0x0000, 0x0000, 0x00c0, 0xef00,		/*reg20-reg26*/
	0x0000, 0x0000, 0x0000, 0x0000,		/*reg28-reg2e*/
	0x0000, 0x0000, 0x0000, 0x0000,		/*reg30-reg36*/
	0x0000, 0x0000, 0x0000, 0x0000, 	/*reg38-reg3e*/
	0x0c0a, 0x0000, 0x0000, 0x0000,		/*reg40-reg46*/
	0x0029, 0x0000, 0xbe3e, 0x3e3e,		/*reg48-reg4e*/
	0x0000, 0x0000, 0x803a, 0x0000,		/*reg50-reg56*/
	0x0000, 0x0009, 0x0000, 0x3000,		/*reg58-reg5e*/
	0x3075, 0x1010, 0x3110, 0x0000,		/*reg60-reg66*/
	0x0553, 0x0000, 0x0000, 0x0000,		/*reg68-reg6e*/
	0x0000, 0x0000, 0x0000, 0x0000,		/*reg70-reg76*/
	0x0000, 0x0000, 0x0000, 0x0000,     /*reg78-reg7e*/
};


Voice_DSP_Reg VODSP_AEC_Init_Value[]=
{
	{0x232C, 0x0025},
	{0x230B, 0x0001},
	{0x2308, 0x007F},
	{0x23F8, 0x4003},
	{0x2301, 0x0002},
	{0x2328, 0x0001},
	{0x2304, 0x00FA},
	{0x2305, 0x0500},
	{0x2306, 0x4000},
	{0x230D, 0x0900},
	{0x230E, 0x0280},
	{0x2312, 0x00B1},
	{0x2314, 0xC000},
	{0x2316, 0x0041},
	{0x2317, 0x2200},
	{0x2318, 0x0C00},
	{0x231D, 0x0050},
	{0x231F, 0x4000},
	{0x2330, 0x0008},
	{0x2335, 0x000A},
	{0x2336, 0x0004},
	{0x2337, 0x5000},
	{0x233A, 0x0300},
	{0x233B, 0x0030},
	{0x2341, 0x0008},
	{0x2343, 0x0800},	
	{0x2352, 0x7FFF},
	{0x237F, 0x0400},
	{0x23A7, 0x2800},
	{0x22CE, 0x0400},
	{0x22D3, 0x1500},
	{0x22D4, 0x2800},
	{0x22D5, 0x3000},
	{0x2399, 0x2800},
	{0x230C, 0x0000},	//to enable VODSP AEC function
};


#define SET_VODSP_REG_INIT_NUM	ARRAY_SIZE(VODSP_AEC_Init_Value)
static struct snd_soc_device *rt5625_socdev;

static inline unsigned int rt5625_read_reg_cache(struct snd_soc_codec *codec, 
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;

	if (reg > 0x7e)
		return 0;
	return cache[reg / 2];
}


static unsigned int rt5625_read_hw_reg(struct snd_soc_codec *codec, unsigned int reg) 
{
	u8 data[2] = {0};
	unsigned int value = 0x0;
	
	data[0] = reg;
	
	i2c_master_reg8_recv(codec->control_data,reg,data,2,100 * 1000);	
	   	      
	value = (data[0]<<8) | data[1];

	DBG(KERN_INFO "rt5625_read ok, reg = %x, value = %x\n", reg, value);

	return value;	
}


static unsigned int rt5625_read(struct snd_soc_codec *codec, unsigned int reg)
{
	if ((reg == 0x80)
		|| (reg == 0x82)
		|| (reg == 0x84))
		return (reg == 0x80) ? reg80 : ((reg == 0x82) ? reg82 : reg84);
	
		return rt5625_read_hw_reg(codec, reg);
}


static inline void rt5625_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg > 0x7E)
		return;
	cache[reg / 2] = value;
}

static int rt5625_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[3];
	unsigned int *regvalue = NULL;

	data[0] = reg;
	data[1] = (value & 0xff00) >> 8;
	data[2] = value & 0x00ff;
	
	if ((reg == 0x80)
		|| (reg == 0x82)
		|| (reg == 0x84))
	{		
		regvalue = ((reg == 0x80) ? &reg80 : ((reg == 0x82) ? &reg82 : &reg84));
		*regvalue = value;
		DBG("rt5625_write ok, reg = %x, value = %x\n", reg, value);
		return 0;
	}
	rt5625_write_reg_cache(codec, reg, value);

	if (codec->hw_write(codec->control_data, data, 3) == 3)
	{
		DBG("rt5625_write ok, reg = %x, value = %x\n", reg, value);
		return 0;
	}
	else 
	{
		printk("rt5625_write fail\n");
		return -EIO;
	}
}

int rt5625_write_mask(struct snd_soc_codec *codec, unsigned int reg,unsigned int value,unsigned int mask)
{
	
	unsigned char RetVal=0;
	unsigned  int CodecData;

	DBG("rt5625_write_mask ok, reg = %x, value = %x ,mask= %x\n", reg, value,mask);

	if(!mask)
		return 0; 

	if(mask!=0xffff)
	 {
		CodecData=rt5625_read(codec,reg);		
		CodecData&=~mask;
		CodecData|=(value&mask);
		RetVal=rt5625_write(codec,reg,CodecData);
	 }		
	else
	{
		RetVal=rt5625_write(codec,reg,value);
	}

	return RetVal;
}


void rt5625_write_index(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	
	rt5625_write(codec,0x6a,reg);
	rt5625_write(codec,0x6c,value);	
}

unsigned int rt5625_read_index(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int value = 0x0;
	rt5625_write(codec,0x6a,reg);
	value=rt5625_read(codec,0x6c);	
	
	return value;
}

void rt5625_write_index_mask(struct snd_soc_codec *codec, unsigned int reg,unsigned int value,unsigned int mask)
{
	
//	unsigned char RetVal=0;
	unsigned  int CodecData;

	DBG("rt5625_write_index_mask ok, reg = %x, value = %x ,mask= %x\n", reg, value,mask);

	if(!mask)
		return; 

	if(mask!=0xffff)
	 {
		CodecData=rt5625_read_index(codec,reg);		
		CodecData&=~mask;
		CodecData|=(value&mask);
		rt5625_write_index(codec,reg,CodecData);
	 }		
	else
	{
		rt5625_write_index(codec,reg,value);
	}
}

//#define rt5625_write_mask(c, reg, value, mask) snd_soc_update_bits(c, reg, mask, value)

#define rt5625_reset(c) rt5625_write(c, RT5625_RESET, 0)

/*read/write dsp reg*/
static int rt5625_wait_vodsp_i2c_done(struct snd_soc_codec *codec)
{
	unsigned int checkcount = 0, vodsp_data;

	vodsp_data = rt5625_read(codec, RT5625_VODSP_REG_CMD);
	while(vodsp_data & VODSP_BUSY)
	{
		if(checkcount > 10)
			return -EBUSY;
		vodsp_data = rt5625_read(codec, RT5625_VODSP_REG_CMD);
		checkcount ++;		
	}
	return 0;
}

static int rt5625_write_vodsp_reg(struct snd_soc_codec *codec, unsigned int vodspreg, unsigned int value)
{
	int ret = 0;

	if(ret != rt5625_wait_vodsp_i2c_done(codec))
		return -EBUSY;

	rt5625_write(codec, RT5625_VODSP_REG_ADDR, vodspreg);
	rt5625_write(codec, RT5625_VODSP_REG_DATA, value);
	rt5625_write(codec, RT5625_VODSP_REG_CMD, VODSP_WRITE_ENABLE | VODSP_CMD_MW);
	mdelay(10);
	return ret;
	
}

static unsigned int rt5625_read_vodsp_reg(struct snd_soc_codec *codec, unsigned int vodspreg)
{
	int ret = 0;
	unsigned int nDataH, nDataL;
	unsigned int value;

	if(ret != rt5625_wait_vodsp_i2c_done(codec))
		return -EBUSY;
	
	rt5625_write(codec, RT5625_VODSP_REG_ADDR, vodspreg);
	rt5625_write(codec, RT5625_VODSP_REG_CMD, VODSP_READ_ENABLE | VODSP_CMD_MR);

	if (ret != rt5625_wait_vodsp_i2c_done(codec))
		return -EBUSY;
	rt5625_write(codec, RT5625_VODSP_REG_ADDR, 0x26);
	rt5625_write(codec, RT5625_VODSP_REG_CMD, VODSP_READ_ENABLE | VODSP_CMD_RR);

	if(ret != rt5625_wait_vodsp_i2c_done(codec))
		return -EBUSY;
	nDataH = rt5625_read(codec, RT5625_VODSP_REG_DATA);
	rt5625_write(codec, RT5625_VODSP_REG_ADDR, 0x25);
	rt5625_write(codec, RT5625_VODSP_REG_CMD, VODSP_READ_ENABLE | VODSP_CMD_RR);

	if(ret != rt5625_wait_vodsp_i2c_done(codec))
		return -EBUSY;
	nDataL = rt5625_read(codec, RT5625_VODSP_REG_DATA);
	value = ((nDataH & 0xff) << 8) |(nDataL & 0xff);
	DBG("%s vodspreg=0x%x, value=0x%x\n", __func__, vodspreg, value);
	return value;
}

static int rt5625_reg_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5625_INIT_REG_NUM; i++)
		rt5625_write(codec, rt5625_init_list[i].reg_index, rt5625_init_list[i].reg_value);

	return 0;
}

//*************************************************************************************************
//*************************************************************************************************
#if (RT5625_EQ_FUNC_ENA==1)	
//eq function
static void rt5625_update_eqmode(struct snd_soc_codec *codec, int mode)
{
	u16 HwEqIndex=0;

	if(mode==NORMAL)
	{
		/*clear EQ parameter*/
		for(HwEqIndex=0;HwEqIndex<=0x0C;HwEqIndex++)
		{
			rt5625_write_index(codec, HwEqIndex, HwEq_Preset[mode].EqValue[HwEqIndex]);
		}
		
		rt5625_write_mask(codec, 0x6e,0x0,EN_HW_EQ_BLK | EN_HW_EQ_HPF | EN_HW_EQ_BP3 | EN_HW_EQ_BP2 | EN_HW_EQ_BP1 | EN_HW_EQ_LPF);		/*disable EQ block*/
	}
	else
	{		
		/*Fill EQ parameter*/
		for(HwEqIndex=0;HwEqIndex<=0x0C;HwEqIndex++)
		{
			rt5625_write_index(codec, HwEqIndex, HwEq_Preset[mode].EqValue[HwEqIndex]); 
		}		

		//enable EQ block
		rt5625_write_mask(codec, 0x6e,HwEq_Preset[mode].HwEQCtrl,EN_HW_EQ_BLK | EN_HW_EQ_HPF | EN_HW_EQ_BP3 | EN_HW_EQ_BP2 | EN_HW_EQ_BP1 | EN_HW_EQ_LPF);		
		
		//update EQ parameter
		rt5625_write_mask(codec, 0x6e,0x0080,0x0080);
	}
}


static int rt5625_eq_sel_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 Virtual_reg = rt5625_read(codec, VIRTUAL_REG_FOR_MISC_FUNC);
	int rt5625_mode=((Virtual_reg)&0xf000)>>12;
	
	if ( rt5625_mode == ucontrol->value.integer.value[0])
		return 0;

	rt5625_update_eqmode(codec, ucontrol->value.enumerated.item[0]);

	Virtual_reg &= 0x0fff;
	Virtual_reg |= (ucontrol->value.integer.value[0])<<12;
	rt5625_write(codec, VIRTUAL_REG_FOR_MISC_FUNC, Virtual_reg);		
	
	return 0;
}
#endif
//*************************************************************************************************
//*************************************************************************************************
static const char *rt5625_aec_path_sel[] = {"aec func disable","aec func for pcm in/out",
                                            "aec func for iis in/out","aec func for analog in/out"};		/*0*/			
static const char *rt5625_spk_out_sel[] = {"Class AB", "Class D"}; 					/*1*/
static const char *rt5625_spk_l_source_sel[] = {"LPRN", "LPRP", "LPLN", "MM"};		/*2*/	
static const char *rt5625_spkmux_source_sel[] = {"VMID", "HP Mixer", 
							"SPK Mixer", "Mono Mixer"};               					/*3*/
static const char *rt5625_hplmux_source_sel[] = {"VMID","HPL Mixer"};				/*4*/
static const char *rt5625_hprmux_source_sel[] = {"VMID","HPR Mixer"};				/*5*/
static const char *rt5625_auxmux_source_sel[] = {"VMID", "HP Mixer", 
							"SPK Mixer", "Mono Mixer"};							/*6*/
static const char *rt5625_spkamp_ratio_sel[] = {"2.25 Vdd", "2.00 Vdd",
					"1.75 Vdd", "1.50 Vdd", "1.25 Vdd", "1.00 Vdd"};				/*7*/
static const char *rt5625_mic1_boost_sel[] = {"Bypass", "+20db", "+30db", "+40db"};	/*8*/
static const char *rt5625_mic2_boost_sel[] = {"Bypass", "+20db", "+30db", "+40db"};	/*9*/
static const char *rt5625_dmic_boost_sel[] = {"Bypass", "+6db", "+12db", "+18db", 
					"+24db", "+30db", "+36db", "+42db"};						/*10*/
static const char *rt5625_adcr_func_sel[] = {"Stereo ADC", "Voice ADC", 
					"VoDSP Interface", "PDM Slave Interface"};                                   /*11*/
#if (RT5625_EQ_FUNC_ENA==1)					
static const char *rt5625_eq_sel[] = {"NORMAL", "CLUB","DANCE", "LIVE","POP",			/*12*/
					"ROCK", "OPPO", "TREBLE", "BASS"};					
#endif

static const struct soc_enum rt5625_enum[] = {

SOC_ENUM_SINGLE(VIRTUAL_REG_FOR_MISC_FUNC, 0, 4, rt5625_aec_path_sel),		/*0*/
SOC_ENUM_SINGLE(RT5625_OUTPUT_MIXER_CTRL, 13, 2, rt5625_spk_out_sel),		/*1*/
SOC_ENUM_SINGLE(RT5625_OUTPUT_MIXER_CTRL, 14, 4, rt5625_spk_l_source_sel),	/*2*/
SOC_ENUM_SINGLE(RT5625_OUTPUT_MIXER_CTRL, 10, 4, rt5625_spkmux_source_sel),/*3*/
SOC_ENUM_SINGLE(RT5625_OUTPUT_MIXER_CTRL, 9, 2, rt5625_hplmux_source_sel),	/*4*/
SOC_ENUM_SINGLE(RT5625_OUTPUT_MIXER_CTRL, 8, 2, rt5625_hprmux_source_sel),/*5*/
SOC_ENUM_SINGLE(RT5625_OUTPUT_MIXER_CTRL, 6, 4, rt5625_auxmux_source_sel),/*6*/
SOC_ENUM_SINGLE(RT5625_GEN_CTRL_REG1, 1, 6, rt5625_spkamp_ratio_sel),		/*7*/
SOC_ENUM_SINGLE(RT5625_MIC_CTRL, 10, 4,  rt5625_mic1_boost_sel),			/*8*/
SOC_ENUM_SINGLE(RT5625_MIC_CTRL, 8, 4, rt5625_mic2_boost_sel),				/*9*/
SOC_ENUM_SINGLE(RT5625_DMIC_CTRL, 0, 8, rt5625_dmic_boost_sel),				/*10*/
SOC_ENUM_SINGLE(RT5625_DAC_ADC_VODAC_FUN_SEL, 4, 4, rt5625_adcr_func_sel), /*11*/
#if (RT5625_EQ_FUNC_ENA==1)
SOC_ENUM_SINGLE(VIRTUAL_REG_FOR_MISC_FUNC, 12, 9, rt5625_eq_sel),    /*EQ mode select mode 12*/
#endif
};



//*****************************************************************************
//function:Enable the Voice PCM interface Path
//*****************************************************************************
static int ConfigPcmVoicePath(struct snd_soc_codec *codec,unsigned int bEnableVoicePath,unsigned int mode)
{

	if(bEnableVoicePath)
	 {
			//Power on DAC reference
			rt5625_write_mask(codec,RT5625_PWR_MANAG_ADD1,PWR_DAC_REF|PWR_VOICE_DF2SE,PWR_DAC_REF|PWR_VOICE_DF2SE);
			//Power on Voice DAC/ADC 
			rt5625_write_mask(codec,RT5625_PWR_MANAG_ADD2,PWR_VOICE_CLOCK,PWR_VOICE_CLOCK);
			//routing voice to HPMixer
			rt5625_write_mask(codec,RT5625_VOICE_DAC_OUT_VOL,0,M_V_DAC_TO_HP_MIXER);
					
		switch(mode)		
		{
			case PCM_SLAVE_MODE_B:	//8kHz sampling rate,16 bits PCM mode and Slave mode,PCM mode is B,MCLK=24.576MHz from Oscillator.
								//CSR PSKEY_PCM_CONFIG32 (HEX) = 0x08C00000,PSKEY_FORMAT=0x0060				

				//Enable GPIO 1,3,4,5 to voice interface
				//Set I2S to Slave mode
				//Voice I2S SYSCLK Source select Main SYSCLK
				//Set voice I2S VBCLK Polarity to Invert
				//Set Data length to 16 bit
				//set Data Fomrat to PCM mode B
				//the register 0x36 value's should is 0xC083
				rt5625_write(codec,RT5625_EXTEND_SDP_CTRL,0xC083);

				//Set LRCK voice select divide 32
				//set voice blck select divide 6 and 8 
				//voice filter clock divide 3 and 16
				//the register 0x64 value's should is 0x5524
				rt5625_write(codec,RT5625_VOICE_DAC_PCMCLK_CTRL1,0x5524);
		
				break;

			case PCM_SLAVE_MODE_A:	//8kHz sampling rate,16 bits PCM and Slave mode,PCM mode is A,MCLK=24.576MHz from Oscillator.
								//CSR PSKEY_PCM_CONFIG32 (HEX) = 0x08C00004,PSKEY_FORMAT=0x0060				

				//Enable GPIO 1,3,4,5 to voice interface
				//Set I2S to Slave mode
				//Voice I2S SYSCLK Source select Main SYSCLK
				//Set voice i2s VBCLK Polarity to Invert
				//Set Data length to 16 bit
				//set Data Fomrat to PCM mode A
				//the register 0x36 value's should is 0xC082
				rt5625_write(codec,RT5625_EXTEND_SDP_CTRL,0xC082);

				//Set LRCK voice select divide 64
				//set voice blck select divide 6 and 8 
				//voice filter clock divide 3 and 16
				//the register 0x64 value's should is 0x5524
				rt5625_write(codec,RT5625_VOICE_DAC_PCMCLK_CTRL1,0x5524);
		
				break;

			case PCM_MASTER_MODE_B:	//8kHz sampling rate,16 bits PCM and Master mode,PCM mode is B,Clock from PLL OUT
								//CSR PSKEY_PCM_CONFIG32 (HEX) = 0x08000002,PSKEY_FORMAT=0x0060	

				//Enable GPIO 1,3,4,5 to voice interface
				//Set I2S to master mode
				//Set voice i2s VBCLK Polarity to Invert
				//Set Data length to 16 bit
				//set Data Fomrat to PCM mode B
				//the register 0x36 value's should is 0x8083
				rt5625_write(codec,RT5625_EXTEND_SDP_CTRL,0x8083);

				//Set LRCK voice select divide 64
				//set voice blck select divide 6 and 8 
				//voice filter clock divide 3 and 16
				//the register 0x64 value's should is 0x5524
				rt5625_write(codec,RT5625_VOICE_DAC_PCMCLK_CTRL1,0x5524);
		
				break;

			default:
				//do nothing		
				break;

			}
		}
 		else
		{	
			//Power down Voice Different to sing-end power
			rt5625_write_mask(codec,RT5625_PWR_MANAG_ADD1,0,PWR_VOICE_DF2SE);
			//Power down Voice DAC/ADC 
			rt5625_write_mask(codec,RT5625_PWR_MANAG_ADD2,0,PWR_VOICE_CLOCK);
			//Disable Voice PCM interface	
			rt5625_write_mask(codec,RT5625_EXTEND_SDP_CTRL,0,EXT_I2S_FUNC_ENABLE);			
		}
	
	return 0;
}

static int init_vodsp_aec(struct snd_soc_codec *codec)
{
	int i;
	int ret = 0;

	/*disable LDO power*/
	rt5625_write_mask(codec, RT5625_LDO_CTRL,0,LDO_ENABLE);
	mdelay(20);	
	rt5625_write_mask(codec, RT5625_VODSP_CTL,VODSP_NO_PD_MODE_ENA,VODSP_NO_PD_MODE_ENA);
	/*enable LDO power and set output voltage to 1.2V*/
	rt5625_write_mask(codec, RT5625_LDO_CTRL,LDO_ENABLE|LDO_OUT_VOL_CTRL_1_20V,LDO_ENABLE|LDO_OUT_VOL_CTRL_MASK);
	mdelay(20);
	/*enable power of VODSP I2C interface*/ 
	rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD3,PWR_VODSP_INTERFACE|PWR_I2C_FOR_VODSP,PWR_VODSP_INTERFACE|PWR_I2C_FOR_VODSP);
	mdelay(1);
	rt5625_write_mask(codec, RT5625_VODSP_CTL,0,VODSP_NO_RST_MODE_ENA);	/*Reset VODSP*/
	mdelay(1);
	rt5625_write_mask(codec, RT5625_VODSP_CTL,VODSP_NO_RST_MODE_ENA,VODSP_NO_RST_MODE_ENA);	/*set VODSP to non-reset status*/		
	mdelay(20);

	/*initize AEC paramter*/
	for(i = 0; i < SET_VODSP_REG_INIT_NUM; i++)
	{
		ret = rt5625_write_vodsp_reg(codec, VODSP_AEC_Init_Value[i].VoiceDSPIndex,VODSP_AEC_Init_Value[i].VoiceDSPValue);
		if(ret)
			return -EIO;
	}		

	schedule_timeout_uninterruptible(msecs_to_jiffies(10));	

	return 0;
}

//***********************************************************************************************
//function:Enable/Disable the vodsp interface Path
//For system clock only suport specific clock,realtek suggest customer to use 24.576Mhz or 22.5792Mhz
//clock fro MCLK(MCLK=48k*512 or 44.1k*512Mhz)
//***********************************************************************************************
static int set_vodsp_aec_path(struct snd_soc_codec *codec, unsigned int mode)
{

		switch(mode)
		{

			case PCM_IN_PCM_OUT:
				//set PCM format
				ConfigPcmVoicePath(codec,1,PCM_MASTER_MODE_B);
				//set AEC path
				rt5625_write_mask(codec, 0x26,0x0300,0x0300);
				rt5625_write_mask(codec, RT5625_VODSP_PDM_CTL,VODSP_RXDP_PWR|VODSP_RXDP_S_SEL_VOICE|VOICE_PCM_S_SEL_AEC_TXDP
															 ,VODSP_RXDP_PWR|VODSP_RXDP_S_SEL_MASK|VOICE_PCM_S_SEL_MASK);
				rt5625_write_mask(codec, RT5625_DAC_ADC_VODAC_FUN_SEL,ADCR_FUNC_SEL_PDM|VODAC_SOUR_SEL_VODSP_TXDC
																	,ADCR_FUNC_SEL_MASK|VODAC_SOUR_SEL_MASK);
				rt5625_write_mask(codec, RT5625_VODSP_CTL,VODSP_LRCK_SEL_8K,VODSP_LRCK_SEL_MASK);						
				rt5625_write_mask(codec, 0x26,0x0000,0x0300);
				//set input&output path and power
				rt5625_write_mask(codec, 0x3a,0x0c8f,0x0c8f);//power on related bit
				rt5625_write_mask(codec, 0x3c,0xa4cb,0xa4cb);//power on related bit
				rt5625_write_mask(codec, 0x3e,0x3302,0xf302);//power on related bit
						
				rt5625_write(codec, 0x10, 0xee0f);//mute DAC to hpmixer
				rt5625_write(codec, 0x0e, 0x8808);//set Mic1 to differential mode
				rt5625_write(codec, 0x22, 0x0000);//Mic boost 0db
				rt5625_write(codec, 0x12, 0xCBD3);//ADC_Mixer_R boost 10.5 db
				rt5625_write(codec, 0x14, 0x7f3f);//Mic1->ADCMixer_R
				rt5625_write(codec, 0x18, 0xa010);//VoDAC to speakerMixer,0db
				rt5625_write(codec, 0x1c, 0x8808);//speaker source from speakermixer
							
				rt5625_write_mask(codec, 0x02,0x0000,0x8080);	//unmute speaker 

				break;
			
			
			case ANALOG_IN_ANALOG_OUT:	
				rt5625_write_mask(codec, 0x26,0x0300,0x0300);
				rt5625_write_mask(codec, RT5625_VODSP_PDM_CTL,VODSP_RXDP_PWR|VODSP_RXDP_S_SEL_ADCL|VOICE_PCM_S_SEL_AEC_TXDP
															 ,VODSP_RXDP_PWR|VODSP_RXDP_S_SEL_MASK|VOICE_PCM_S_SEL_MASK);
				rt5625_write_mask(codec, RT5625_DAC_ADC_VODAC_FUN_SEL,ADCR_FUNC_SEL_PDM|VODAC_SOUR_SEL_VODSP_TXDC|DAC_FUNC_SEL_VODSP_TXDP|ADCL_FUNC_SEL_VODSP
																	,ADCR_FUNC_SEL_MASK|VODAC_SOUR_SEL_MASK|DAC_FUNC_SEL_MASK|ADCL_FUNC_SEL_MASK);
				rt5625_write_mask(codec, RT5625_VODSP_CTL,VODSP_LRCK_SEL_16K,VODSP_LRCK_SEL_MASK);	
				rt5625_write_mask(codec, 0x26,0x0000,0x0300);
				//set input&output path and power
				rt5625_write_mask(codec, 0x3a,0xcc8f,0xcc8f);//power on related bit
				rt5625_write_mask(codec, 0x3c,0xa7cf,0xa7cf);//power on related bit
				rt5625_write_mask(codec, 0x3e,0xf312,0xf312);//power on related bit
				
				rt5625_write(codec, 0x0e, 0x8808);//set Mic1 to differential mode
				rt5625_write(codec, 0x08, 0xe800);//set phone in to differential mode
				rt5625_write(codec, 0x22, 0x0000);//Mic boost 0db
				rt5625_write(codec, 0x14, 0x773f);//Mic1->ADCMixer_R,phone in-->ADCMixer_L
				rt5625_write(codec, 0x12, 0xCBD3);//ADC_Mixer_R boost 10.5 db
				rt5625_write(codec, 0x1c, 0x88c8);//speaker from spkmixer,monoOut from monoMixer
				rt5625_write(codec, 0x18, 0xA010);//unmute VoDAC to spkmixer
				rt5625_write(codec, 0x10, 0xee0e);//unmute DAC to monoMixer	
				rt5625_write(codec, 0x62, 0x2222);
				rt5625_write(codec, 0x64, 0x3122);
				rt5625_write_mask(codec, 0x02,0x0000,0x8080);	//unmute speaker 
				rt5625_write_mask(codec, 0x06,0x0000,0x8080);	//unmute auxout	
				break;

			case DAC_IN_ADC_OUT:	
				rt5625_write_mask(codec, 0x26,0x0300,0x0300);
				rt5625_write_mask(codec,RT5625_DAC_ADC_VODAC_FUN_SEL,ADCR_FUNC_SEL_PDM|DAC_FUNC_SEL_VODSP_TXDC
																	,ADCR_FUNC_SEL_MASK|DAC_FUNC_SEL_MASK);
				rt5625_write_mask(codec,RT5625_VODSP_PDM_CTL,VODSP_SRC1_PWR|VODSP_SRC2_PWR|VODSP_RXDP_PWR|VODSP_RXDP_S_SEL_SRC1|REC_S_SEL_SRC2,
															 VODSP_SRC1_PWR|VODSP_SRC2_PWR|VODSP_RXDP_PWR|VODSP_RXDP_S_SEL_MASK|REC_S_SEL_MASK);					
				rt5625_write_mask(codec,RT5625_VODSP_CTL,VODSP_LRCK_SEL_16K,VODSP_LRCK_SEL_MASK);
				rt5625_write_mask(codec, 0x26,0x0000,0x0300);
				//set input&output path and power	
				rt5625_write_mask(codec, 0x3a,0xcc0f,0xcc0f);//power on related bit
				rt5625_write_mask(codec, 0x3c,0xa7cb,0xa7cb);//power on related bit
				rt5625_write_mask(codec, 0x3e,0x3302,0x3302);//power on related bit
				
				rt5625_write(codec, 0x0e, 0x8808);//set Mic1 to differential mode
				rt5625_write(codec, 0x22, 0x0000);//Mic boost 0db
				rt5625_write(codec, 0x14, 0x7f3f);//Mic1->ADCMixer_R
				rt5625_write(codec, 0x12, 0xCBD3);//ADC_Mixer_R boost 10.5 db
				rt5625_write(codec, 0x1c, 0x8808);//speaker out from spkMixer
				rt5625_write(codec, 0x10, 0xee0d);//unmute DAC to spkMixer	
				rt5625_write(codec, 0x60, 0x3075);
				rt5625_write(codec, 0x62, 0x1010);					
				rt5625_write_mask(codec, 0x02,0x0000,0x8080);	//unmute speaker 

				break;

			case VODSP_AEC_DISABLE:
			default:
				rt5625_write_mask(codec, 0x02,0x8080,0x8080);//mute speaker out
				rt5625_write_mask(codec, 0x06,0x8080,0x8080);//mute auxout		
				rt5625_write(codec, 0x22, 0x0500);//Mic boost 20db by default
				rt5625_write(codec, 0x14, 0x3f3f);//record from Mic1 by default
				rt5625_write(codec, 0x12, 0xD5D5);//ADC_Mixer_R boost 15 db by default
				rt5625_write(codec, 0x1c, 0x0748);//all output from HPmixer by default
				rt5625_write(codec, 0x10, 0xee03);//DAC to HPmixer by default
				rt5625_write(codec, 0x18, 0xe010);//mute VoDAC to mixer by default
				rt5625_write_mask(codec, 0x26,0x0300,0x0300);
				/*set stereo DAC&Voice DAC&Stereo ADC function select to default*/ 
				rt5625_write(codec, RT5625_DAC_ADC_VODAC_FUN_SEL,0);		
				/*set VODSP&PDM Control to default*/ 
				rt5625_write(codec, RT5625_VODSP_PDM_CTL,0);
				rt5625_write_mask(codec, 0x26,0x0000,0x0300);		
				rt5625_write_mask(codec, 0x3e,0x0000,0xf312);//power down related bit
				rt5625_write_mask(codec, 0x3a,0x0000,0xcc8d);//power down related bit
				rt5625_write_mask(codec, 0x3c,0x0000,0x07cf);//power down related bit
				
			
				break;
		}		

	return 0;
}

static int enable_vodsp_aec(struct snd_soc_codec *codec, unsigned int VodspAEC_En, unsigned int AEC_mode)
{
	int  ret = 0;

	
	if (VodspAEC_En != 0)
	{	

		//enable power of VODSP I2C interface & VODSP interface
		rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD3,PWR_VODSP_INTERFACE|PWR_I2C_FOR_VODSP,PWR_VODSP_INTERFACE|PWR_I2C_FOR_VODSP);
		//enable power of VODSP I2S interface 
		rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD1,PWR_I2S_INTERFACE,PWR_I2S_INTERFACE);	
		//select input/output of VODSP AEC
		set_vodsp_aec_path(codec, AEC_mode);		

	}
	else
	{
		//disable VODSP AEC path
		set_vodsp_aec_path(codec, VODSP_AEC_DISABLE);
		//set VODSP AEC to power down mode			
		rt5625_write_mask(codec, RT5625_VODSP_CTL,0,VODSP_NO_PD_MODE_ENA);
		//disable power of VODSP I2C interface & VODSP interface
		rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD3,0,PWR_VODSP_INTERFACE|PWR_I2C_FOR_VODSP);				
	}

	return ret;
}

static void rt5625_aec_config(struct snd_soc_codec *codec, unsigned int mode)
{
	DBG("rt5625_aec_config %d\n",mode);

	if (mode == VODSP_AEC_DISABLE)
	{
		enable_vodsp_aec(codec,0, mode);	
		/*disable LDO power*/
		rt5625_write_mask(codec, RT5625_LDO_CTRL,0,LDO_ENABLE);
	}
	else
	{
		init_vodsp_aec(codec);
	
		enable_vodsp_aec(codec,1, mode);		
	}
}
//****************************************************************************************************************
//*
//*function:disable rt5625's function.
//*
//*
//****************************************************************************************************************
static int rt5625_func_aec_disable(struct snd_soc_codec *codec,int mode)
{

	switch(mode)
	{
		case RT5625_AEC_PCM_IN_OUT:
		case RT5625_AEC_IIS_IN_OUT:
		case RT5625_AEC_ANALOG_IN_OUT:
			
			rt5625_aec_config(codec,VODSP_AEC_DISABLE);	//disable AEC function and path
			
		break;
		
		default:

		break;
	}

	return 0;
}


static int rt5625_get_dsp_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	 /*cause we choose bit[0][1] to store the mode type*/
	int mode = (rt5625_read(codec, VIRTUAL_REG_FOR_MISC_FUNC)) & 0x03;  

	ucontrol->value.integer.value[0] = mode;
	return 0;
}


static int rt5625_set_dsp_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	u16 Virtual_reg = rt5625_read(codec, VIRTUAL_REG_FOR_MISC_FUNC);
	int rt5625_mode=(Virtual_reg)&0x03;

	DBG("rt5625_mode=%d,value[0]=%ld,Virtual_reg=%x\n",rt5625_mode,ucontrol->value.integer.value[0],Virtual_reg);

	if ( rt5625_mode == ucontrol->value.integer.value[0])
		return 0;

	switch(ucontrol->value.integer.value[0])
	{
		case RT5625_AEC_PCM_IN_OUT:

			rt5625_aec_config(codec,PCM_IN_PCM_OUT);//enable AEC PCM in/out function and path

		break;

		case RT5625_AEC_IIS_IN_OUT:

			rt5625_aec_config(codec,DAC_IN_ADC_OUT);//enable AEC IIS in/out function and path

		break;

		case RT5625_AEC_ANALOG_IN_OUT:
			
			rt5625_aec_config(codec,ANALOG_IN_ANALOG_OUT);//enable AEC analog in/out function and path
			
		break;

		case RT5625_AEC_DISABLE:
	
			rt5625_func_aec_disable(codec,rt5625_mode);		//disable previous select function.	
	
		break;	

		default:

		break;
	}

	Virtual_reg &= 0xfffc;
	Virtual_reg |= (ucontrol->value.integer.value[0]);
	rt5625_write(codec, VIRTUAL_REG_FOR_MISC_FUNC, Virtual_reg);

	DBG("2rt5625_mode=%d,value[0]=%ld,Virtual_reg=%x\n",rt5625_mode,ucontrol->value.integer.value[0],Virtual_reg);
	return 0;
}

static int rt5625_dump_dsp_reg(struct snd_soc_codec *codec)
{
	int i;

	rt5625_write_mask(codec, RT5625_VODSP_CTL, VODSP_NO_PD_MODE_ENA,VODSP_NO_PD_MODE_ENA);
	for (i = 0; i < SET_VODSP_REG_INIT_NUM; i++) {
		rt5625_read_vodsp_reg(codec, VODSP_AEC_Init_Value[i].VoiceDSPIndex);
	}
	return 0;
}


static int rt5625_dump_dsp_put(struct snd_kcontrol *kcontrol, 
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int mode = rt5625_read(codec, VIRTUAL_REG_FOR_MISC_FUNC);

	mode &= ~(0x01 << 8);
	mode |= (ucontrol->value.integer.value[0] << 8);
	rt5625_write(codec, VIRTUAL_REG_FOR_MISC_FUNC, mode);
	rt5625_dump_dsp_reg(codec);
	
	return 0;
}

static const struct snd_kcontrol_new rt5625_snd_controls[] = {
SOC_ENUM_EXT("rt5625 aec mode sel", rt5625_enum[0], rt5625_get_dsp_mode, rt5625_set_dsp_mode),
SOC_ENUM("SPK Amp Type", rt5625_enum[1]),
SOC_ENUM("Left SPK Source", rt5625_enum[2]),
SOC_ENUM("SPK Amp Ratio", rt5625_enum[7]),
SOC_ENUM("Mic1 Boost", rt5625_enum[8]),
SOC_ENUM("Mic2 Boost", rt5625_enum[9]),
SOC_ENUM("Dmic Boost", rt5625_enum[10]),
SOC_ENUM("ADCR Func", rt5625_enum[11]),
SOC_DOUBLE("PCM Playback Volume", RT5625_STEREO_DAC_VOL, 8, 0, 63, 1),
SOC_DOUBLE("LineIn Playback Volume", RT5625_LINE_IN_VOL, 8, 0, 31, 1),
SOC_SINGLE("Phone Playback Volume", RT5625_PHONEIN_VOL, 8, 31, 1),
SOC_SINGLE("Mic1 Playback Volume", RT5625_MIC_VOL, 8, 31, 1),
SOC_SINGLE("Mic2 Playback Volume", RT5625_MIC_VOL, 0, 31, 1),
SOC_DOUBLE("PCM Capture Volume", RT5625_ADC_REC_GAIN, 8, 0, 31, 1),
SOC_DOUBLE("SPKOUT Playback Volume", RT5625_SPK_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("SPKOUT Playback Switch", RT5625_SPK_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("HPOUT Playback Volume", RT5625_HP_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("HPOUT Playback Switch", RT5625_HP_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("AUXOUT Playback Volume", RT5625_AUX_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("AUXOUT Playback Switch", RT5625_AUX_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("ADC Record Gain", RT5625_ADC_REC_GAIN, 8, 0, 31, 0),
SOC_SINGLE_EXT("VoDSP Dump", VIRTUAL_REG_FOR_MISC_FUNC, 8, 1, 0,
			snd_soc_get_volsw, rt5625_dump_dsp_put),
#if (RT5625_EQ_FUNC_ENA==1)			
SOC_ENUM_EXT("EQ Mode", rt5625_enum[12], snd_soc_get_enum_double, rt5625_eq_sel_put),				
#endif
};

static int rt5625_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(rt5625_snd_controls); i++){
		err = snd_ctl_add(codec->card, 
				snd_soc_cnew(&rt5625_snd_controls[i],
						codec, NULL));
		if (err < 0)
			return err;
	}
	return 0;
}

static void hp_depop_mode2(struct snd_soc_codec *codec)
{
        rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD1, PWR_SOFTGEN_EN, PWR_SOFTGEN_EN);
        rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD3, PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL,
	                  PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL);
        rt5625_write(codec, RT5625_MISC_CTRL,HP_DEPOP_MODE2_EN);

	DBG("delay 500 msec\n");

        schedule_timeout_uninterruptible(msecs_to_jiffies(500));
		

        rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD1, PWR_HP_OUT_AMP|PWR_HP_OUT_ENH_AMP,
	                  PWR_HP_OUT_AMP|PWR_HP_OUT_ENH_AMP);
	//rt5625_write_mask(codec, RT5625_MISC_CTRL, 0, HP_DEPOP_MODE2_EN);

}

//enable depop function for mute/unmute
static void hp_mute_unmute_depop(struct snd_soc_codec *codec,int mute)
{
 	if(mute)
	{
		rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD1, PWR_SOFTGEN_EN, PWR_SOFTGEN_EN);
		rt5625_write(codec, RT5625_MISC_CTRL,M_UM_DEPOP_EN|HP_R_M_UM_DEPOP_EN|HP_L_M_UM_DEPOP_EN);
		//Mute headphone right/left channel
		rt5625_write_mask(codec,RT5625_HP_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	
		mdelay(50);
	}
	else
	{
		rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD1, PWR_SOFTGEN_EN, PWR_SOFTGEN_EN);
		rt5625_write(codec, RT5625_MISC_CTRL, M_UM_DEPOP_EN|HP_R_M_UM_DEPOP_EN|HP_L_M_UM_DEPOP_EN);
		//unMute headphone right/left channel
		rt5625_write_mask(codec,RT5625_HP_OUT_VOL,0,RT_L_MUTE|RT_R_MUTE);	
		mdelay(50);
	}

}


/*
 * _DAPM_ Controls
 */
 /*Left ADC Rec mixer*/
 /*Left ADC Rec mixer*/
static const struct snd_kcontrol_new rt5625_left_adc_rec_mixer_controls[] = {
SOC_DAPM_SINGLE("Mic1 Capture Switch", RT5625_ADC_REC_MIXER, 14, 1, 1),
SOC_DAPM_SINGLE("Mic2 Capture Switch", RT5625_ADC_REC_MIXER, 13, 1, 1),
SOC_DAPM_SINGLE("LineIn Capture Switch", RT5625_ADC_REC_MIXER, 12, 1, 1),
SOC_DAPM_SINGLE("Phone Capture Switch", RT5625_ADC_REC_MIXER, 11, 1, 1),
SOC_DAPM_SINGLE("HP Mixer Capture Switch", RT5625_ADC_REC_MIXER, 10, 1, 1),
SOC_DAPM_SINGLE("SPK Mixer Capture Switch", RT5625_ADC_REC_MIXER, 9, 1, 1),
SOC_DAPM_SINGLE("MoNo Mixer Capture Switch", RT5625_ADC_REC_MIXER, 8, 1, 1),

};

/*Left ADC Rec mixer*/
static const struct snd_kcontrol_new rt5625_right_adc_rec_mixer_controls[] = {
SOC_DAPM_SINGLE("Mic1 Capture Switch", RT5625_ADC_REC_MIXER, 6, 1, 1),
SOC_DAPM_SINGLE("Mic2 Capture Switch", RT5625_ADC_REC_MIXER, 5, 1, 1),
SOC_DAPM_SINGLE("LineIn Capture Switch", RT5625_ADC_REC_MIXER, 4, 1, 1),
SOC_DAPM_SINGLE("Phone Capture Switch", RT5625_ADC_REC_MIXER, 3, 1, 1),
SOC_DAPM_SINGLE("HP Mixer Capture Switch", RT5625_ADC_REC_MIXER, 2, 1, 1),
SOC_DAPM_SINGLE("SPK Mixer Capture Switch", RT5625_ADC_REC_MIXER, 1, 1, 1),
SOC_DAPM_SINGLE("MoNo Mixer Capture Switch", RT5625_ADC_REC_MIXER, 0, 1, 1),
};

/*Left hpmixer mixer*/
static const struct snd_kcontrol_new rt5625_left_hp_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC Playback Switch", RT5625_ADC_REC_GAIN, 15, 1, 1),
SOC_DAPM_SINGLE("LineIn Playback Switch", HPL_MIXER, 0, 1, 0),
SOC_DAPM_SINGLE("Phone Playback Switch", HPL_MIXER, 1, 1, 0),
SOC_DAPM_SINGLE("Mic1 Playback Switch", HPL_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("Mic2 Playback Switch", HPL_MIXER, 3, 1, 0),
SOC_DAPM_SINGLE("Voice DAC Playback Switch", HPL_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("HIFI DAC Playback Switch", RT5625_DAC_AND_MIC_CTRL, 3, 1, 1),

};

/*Right hpmixer mixer*/
static const struct snd_kcontrol_new rt5625_right_hp_mixer_controls[] = {
SOC_DAPM_SINGLE("ADC Playback Switch", RT5625_ADC_REC_GAIN, 7, 1, 1),
SOC_DAPM_SINGLE("LineIn Playback Switch", HPR_MIXER, 0, 1, 0),
SOC_DAPM_SINGLE("Phone Playback Switch", HPR_MIXER, 1, 1, 0),
SOC_DAPM_SINGLE("Mic1 Playback Switch", HPR_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("Mic2 Playback Switch", HPR_MIXER, 3, 1, 0),
SOC_DAPM_SINGLE("Voice DAC Playback Switch", HPR_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("HIFI DAC Playback Switch", RT5625_DAC_AND_MIC_CTRL, 2, 1, 1),

};

/*mono mixer*/
static const struct snd_kcontrol_new rt5625_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("ADCL Playback Switch", RT5625_ADC_REC_GAIN, 14, 1, 1),
SOC_DAPM_SINGLE("ADCR Playback Switch", RT5625_ADC_REC_GAIN, 6, 1, 1),
SOC_DAPM_SINGLE("Line Mixer Playback Switch", RT5625_LINE_IN_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5625_DAC_AND_MIC_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5625_DAC_AND_MIC_CTRL, 9, 1, 1),
SOC_DAPM_SINGLE("DAC Mixer Playback Switch", RT5625_DAC_AND_MIC_CTRL, 0, 1, 1),
SOC_DAPM_SINGLE("Voice DAC Playback Switch", RT5625_VOICE_DAC_OUT_VOL, 13, 1, 1),
};

/*speaker mixer*/
static const struct snd_kcontrol_new rt5625_spk_mixer_controls[] = {
SOC_DAPM_SINGLE("Line Mixer Playback Switch", RT5625_LINE_IN_VOL, 14, 1, 1),	
SOC_DAPM_SINGLE("Phone Playback Switch", RT5625_PHONEIN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5625_DAC_AND_MIC_CTRL, 14, 1, 1),
SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5625_DAC_AND_MIC_CTRL, 10, 1, 1),
SOC_DAPM_SINGLE("DAC Mixer Playback Switch", RT5625_DAC_AND_MIC_CTRL, 1, 1, 1),
SOC_DAPM_SINGLE("Voice DAC Playback Switch", RT5625_VOICE_DAC_OUT_VOL, 14, 1, 1),
};

static int mixer_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int l, r;

	DBG("enter %s\n", __func__);

	l= rt5625_read(codec, HPL_MIXER);
	r = rt5625_read(codec, HPR_MIXER);
	
	if ((l & 0x1) || (r & 0x1))
		rt5625_write_mask(codec, 0x0a, 0x0000, 0x8000);
	else
		rt5625_write_mask(codec, 0x0a, 0x8000, 0x8000);

	if ((l & 0x2) || (r & 0x2))
		rt5625_write_mask(codec, 0x08, 0x0000, 0x8000);
	else
		rt5625_write_mask(codec, 0x08, 0x8000, 0x8000);

	if ((l & 0x4) || (r & 0x4))
		rt5625_write_mask(codec, 0x10, 0x0000, 0x8000);
	else
		rt5625_write_mask(codec, 0x10, 0x8000, 0x8000);

	if ((l & 0x8) || (r & 0x8))
		rt5625_write_mask(codec, 0x10, 0x0000, 0x0800);
	else
		rt5625_write_mask(codec, 0x10, 0x0800, 0x0800);

	if ((l & 0x10) || (r & 0x10))
		rt5625_write_mask(codec, 0x18, 0x0000, 0x8000);
	else
		rt5625_write_mask(codec, 0x18, 0x8000, 0x8000);

	return 0;
}


/*
 *	bit[0][1] use for aec control
 *	bit[2][3] for ADCR func
 *	bit[4] for SPKL pga
 *	bit[5] for SPKR pga
 *	bit[6] for hpl pga
 *	bit[7] for hpr pga
 */
static int spk_pga_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
 {
	struct snd_soc_codec *codec = w->codec;
	int reg;
	
	DBG("enter %s\n", __func__);
	reg = rt5625_read(codec, VIRTUAL_REG_FOR_MISC_FUNC) & (0x3 << 4);
	if ((reg >> 4) != 0x3 && reg != 0)
		return 0;

	switch (event)
	{
		case SND_SOC_DAPM_POST_PMU:
			DBG("after virtual spk power up!\n");
			rt5625_write_mask(codec, 0x3e, 0x3000, 0x3000);
			rt5625_write_mask(codec, 0x02, 0x0000, 0x8080);
			rt5625_write_mask(codec, 0x3a, 0x0400, 0x0400);//power on spk amp
			break;
		case SND_SOC_DAPM_POST_PMD:
			DBG("aftet virtual spk power down!\n");
			rt5625_write_mask(codec, 0x3a, 0x0000, 0x0400);//power off spk amp
			rt5625_write_mask(codec, 0x02, 0x8080, 0x8080);
			rt5625_write_mask(codec, 0x3e, 0x0000, 0x3000);                 
			break;
		default:
			return 0;
	}
	return 0;
}




static int hp_pga_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	struct snd_soc_codec *codec = w->codec;
	int reg;

	DBG("enter %s\n", __func__);

	reg = rt5625_read(codec, VIRTUAL_REG_FOR_MISC_FUNC) & (0x3 << 6);
	if ((reg >> 6) != 0x3 && reg != 0)
		return 0;
	
	switch (event)
	{
		case SND_SOC_DAPM_POST_PMD:

			DBG("aftet virtual hp power down!\n");

			hp_mute_unmute_depop(codec,1);//mute hp
			rt5625_write_mask(codec, 0x3a, 0x0000, 0x0300);
			rt5625_write_mask(codec, 0x3e, 0x0000, 0x0c00);			
			break;

		case SND_SOC_DAPM_POST_PMU:	

			DBG("after virtual hp power up!\n");
			hp_depop_mode2(codec);
			hp_mute_unmute_depop(codec,0);//unmute hp
			break;

		default:
			return 0;
	}	

	return 0;
}



static int aux_pga_event(struct snd_soc_dapm_widget *w, struct snd_kcontrol *k, int event)
{
	return 0;
}

/*SPKOUT Mux*/
static const struct snd_kcontrol_new rt5625_spkout_mux_out_controls = 
SOC_DAPM_ENUM("Route", rt5625_enum[3]);

/*HPLOUT MUX*/
static const struct snd_kcontrol_new rt5625_hplout_mux_out_controls = 
SOC_DAPM_ENUM("Route", rt5625_enum[4]);

/*HPROUT MUX*/
static const struct snd_kcontrol_new rt5625_hprout_mux_out_controls = 
SOC_DAPM_ENUM("Route", rt5625_enum[5]);
/*AUXOUT MUX*/
static const struct snd_kcontrol_new rt5625_auxout_mux_out_controls = 
SOC_DAPM_ENUM("Route", rt5625_enum[6]);

static const struct snd_soc_dapm_widget rt5625_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("Left LineIn"),
SND_SOC_DAPM_INPUT("Right LineIn"),
SND_SOC_DAPM_INPUT("Phone"),
SND_SOC_DAPM_INPUT("Mic1"),
SND_SOC_DAPM_INPUT("Mic2"),

SND_SOC_DAPM_PGA("Mic1 Boost", RT5625_PWR_MANAG_ADD3, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic2 Boost", RT5625_PWR_MANAG_ADD3, 0, 0, NULL, 0),

SND_SOC_DAPM_DAC("Left DAC", "Left HiFi Playback DAC", RT5625_PWR_MANAG_ADD2, 9, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right HiFi Playback DAC", RT5625_PWR_MANAG_ADD2, 8, 0),
SND_SOC_DAPM_DAC("Voice DAC", "Voice Playback DAC", RT5625_PWR_MANAG_ADD2, 10, 0),

SND_SOC_DAPM_PGA("Left LineIn PGA", RT5625_PWR_MANAG_ADD3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right LineIn PGA", RT5625_PWR_MANAG_ADD3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Phone PGA", RT5625_PWR_MANAG_ADD3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic1 PGA", RT5625_PWR_MANAG_ADD3, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic2 PGA", RT5625_PWR_MANAG_ADD3, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("VoDAC PGA", RT5625_PWR_MANAG_ADD1, 7, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Left Rec Mixer", RT5625_PWR_MANAG_ADD2, 1, 0,
	&rt5625_left_adc_rec_mixer_controls[0], ARRAY_SIZE(rt5625_left_adc_rec_mixer_controls)),
SND_SOC_DAPM_MIXER("Right Rec Mixer", RT5625_PWR_MANAG_ADD2, 0, 0,
	&rt5625_right_adc_rec_mixer_controls[0], ARRAY_SIZE(rt5625_right_adc_rec_mixer_controls)),
SND_SOC_DAPM_MIXER_E("Left HP Mixer", RT5625_PWR_MANAG_ADD2, 5, 0,
	&rt5625_left_hp_mixer_controls[0], ARRAY_SIZE(rt5625_left_hp_mixer_controls),
	mixer_event, SND_SOC_DAPM_POST_REG),
SND_SOC_DAPM_MIXER_E("Right HP Mixer", RT5625_PWR_MANAG_ADD2, 4, 0,
	&rt5625_right_hp_mixer_controls[0], ARRAY_SIZE(rt5625_right_hp_mixer_controls),
	mixer_event, SND_SOC_DAPM_POST_REG),
SND_SOC_DAPM_MIXER("MoNo Mixer", RT5625_PWR_MANAG_ADD2, 2, 0, 
	&rt5625_mono_mixer_controls[0], ARRAY_SIZE(rt5625_mono_mixer_controls)),
SND_SOC_DAPM_MIXER("SPK Mixer", RT5625_PWR_MANAG_ADD2, 3, 0,
	&rt5625_spk_mixer_controls[0], ARRAY_SIZE(rt5625_spk_mixer_controls)),	
SND_SOC_DAPM_MIXER("HP Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("DAC Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("Line Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MUX("SPKOUT Mux", SND_SOC_NOPM, 0, 0, &rt5625_spkout_mux_out_controls),
SND_SOC_DAPM_MUX("HPLOUT Mux", SND_SOC_NOPM, 0, 0, &rt5625_hplout_mux_out_controls),
SND_SOC_DAPM_MUX("HPROUT Mux", SND_SOC_NOPM, 0, 0, &rt5625_hprout_mux_out_controls),
SND_SOC_DAPM_MUX("AUXOUT Mux", SND_SOC_NOPM, 0, 0, &rt5625_auxout_mux_out_controls),

SND_SOC_DAPM_PGA_E("SPKL Out PGA", VIRTUAL_REG_FOR_MISC_FUNC, 4, 0, NULL, 0,
				spk_pga_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_PGA_E("SPKR Out PGA", VIRTUAL_REG_FOR_MISC_FUNC, 5, 0, NULL, 0,
				spk_pga_event, SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
SND_SOC_DAPM_PGA_E("HPL Out PGA",VIRTUAL_REG_FOR_MISC_FUNC, 6, 0, NULL, 0, 
				hp_pga_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("HPR Out PGA",VIRTUAL_REG_FOR_MISC_FUNC, 7, 0, NULL, 0, 
				hp_pga_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("AUX Out PGA",RT5625_PWR_MANAG_ADD3, 14, 0, NULL, 0, 
				aux_pga_event, SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
				
SND_SOC_DAPM_ADC("Left ADC", "Left ADC HiFi Capture", RT5625_PWR_MANAG_ADD2, 7, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right ADC HiFi Capture", RT5625_PWR_MANAG_ADD2, 6, 0),
SND_SOC_DAPM_OUTPUT("SPKL"),
SND_SOC_DAPM_OUTPUT("SPKR"),
SND_SOC_DAPM_OUTPUT("HPL"),
SND_SOC_DAPM_OUTPUT("HPR"),
SND_SOC_DAPM_OUTPUT("AUX"),
SND_SOC_DAPM_MICBIAS("Mic1 Bias", RT5625_PWR_MANAG_ADD1, 3, 0),
SND_SOC_DAPM_MICBIAS("Mic2 Bias", RT5625_PWR_MANAG_ADD1, 2, 0),
};

static const struct snd_soc_dapm_route audio_map[] = {
		/*Input PGA*/

		{"Left LineIn PGA", NULL, "Left LineIn"},
		{"Right LineIn PGA", NULL, "Right LineIn"},
		{"Phone PGA", NULL, "Phone"},
		{"Mic1 Boost", NULL, "Mic1"},
		{"Mic2 Boost", NULL, "Mic2"},
		{"Mic1 PGA", NULL, "Mic1"},
		{"Mic2 PGA", NULL, "Mic2"},
		{"VoDAC PGA", NULL, "Voice DAC"},
		
		/*Left ADC mixer*/
		{"Left Rec Mixer", "LineIn Capture Switch", "Left LineIn"},
		{"Left Rec Mixer", "Phone Capture Switch", "Phone"},
		{"Left Rec Mixer", "Mic1 Capture Switch", "Mic1 Boost"},
		{"Left Rec Mixer", "Mic2 Capture Switch", "Mic2 Boost"},
		{"Left Rec Mixer", "HP Mixer Capture Switch", "Left HP Mixer"},
		{"Left Rec Mixer", "SPK Mixer Capture Switch", "SPK Mixer"},
		{"Left Rec Mixer", "MoNo Mixer Capture Switch", "MoNo Mixer"},

		/*Right ADC Mixer*/
		{"Right Rec Mixer", "LineIn Capture Switch", "Right LineIn"},
		{"Right Rec Mixer", "Phone Capture Switch", "Phone"},
		{"Right Rec Mixer", "Mic1 Capture Switch", "Mic1 Boost"},
		{"Right Rec Mixer", "Mic2 Capture Switch", "Mic2 Boost"},
		{"Right Rec Mixer", "HP Mixer Capture Switch", "Right HP Mixer"},
		{"Right Rec Mixer", "SPK Mixer Capture Switch", "SPK Mixer"},
		{"Right Rec Mixer", "MoNo Mixer Capture Switch", "MoNo Mixer"},
		
		/*HPL mixer*/
		{"Left HP Mixer", "ADC Playback Switch", "Left Rec Mixer"},
		{"Left HP Mixer", "LineIn Playback Switch", "Left LineIn PGA"},
		{"Left HP Mixer", "Phone Playback Switch", "Phone PGA"},
		{"Left HP Mixer", "Mic1 Playback Switch", "Mic1 PGA"},
		{"Left HP Mixer", "Mic2 Playback Switch", "Mic2 PGA"},
		{"Left HP Mixer", "HIFI DAC Playback Switch", "Left DAC"},
		{"Left HP Mixer", "Voice DAC Playback Switch", "VoDAC PGA"},
		
		/*HPR mixer*/
		{"Right HP Mixer", "ADC Playback Switch", "Right Rec Mixer"},
		{"Right HP Mixer", "LineIn Playback Switch", "Right LineIn PGA"},
		{"Right HP Mixer", "HIFI DAC Playback Switch", "Right DAC"},
		{"Right HP Mixer", "Phone Playback Switch", "Phone PGA"},
		{"Right HP Mixer", "Mic1 Playback Switch", "Mic1 PGA"},
		{"Right HP Mixer", "Mic2 Playback Switch", "Mic2 PGA"},
		{"Right HP Mixer", "Voice DAC Playback Switch", "VoDAC PGA"},

		/*DAC Mixer*/
		{"DAC Mixer", NULL, "Left DAC"},
		{"DAC Mixer", NULL, "Right DAC"},

		/*line mixer*/
		{"Line Mixer", NULL, "Left LineIn PGA"},
		{"Line Mixer", NULL, "Right LineIn PGA"},

		/*spk mixer*/
		{"SPK Mixer", "Line Mixer Playback Switch", "Line Mixer"},
		{"SPK Mixer", "Phone Playback Switch", "Phone PGA"},
		{"SPK Mixer", "Mic1 Playback Switch", "Mic1 PGA"},
		{"SPK Mixer", "Mic2 Playback Switch", "Mic2 PGA"},
		{"SPK Mixer", "DAC Mixer Playback Switch", "DAC Mixer"},
		{"SPK Mixer", "Voice DAC Playback Switch", "VoDAC PGA"},

		/*mono mixer*/
		{"MoNo Mixer", "Line Mixer Playback Switch", "Line Mixer"},
		{"MoNo Mixer", "ADCL Playback Switch","Left Rec Mixer"},
		{"MoNo Mixer", "ADCR Playback Switch","Right Rec Mixer"},
		{"MoNo Mixer", "Mic1 Playback Switch", "Mic1 PGA"},
		{"MoNo Mixer", "Mic2 Playback Switch", "Mic2 PGA"},
		{"MoNo Mixer", "DAC Mixer Playback Switch", "DAC Mixer"},
		{"MoNo Mixer", "Voice DAC Playback Switch", "VoDAC PGA"},
		
		/*hp mixer*/
		{"HP Mixer", NULL, "Left HP Mixer"},
		{"HP Mixer", NULL, "Right HP Mixer"},

		/*spkout mux*/
		{"SPKOUT Mux", "HP Mixer", "HP Mixer"},
		{"SPKOUT Mux", "SPK Mixer", "SPK Mixer"},
		{"SPKOUT Mux", "Mono Mixer", "MoNo Mixer"},
		
		/*hpl out mux*/
		{"HPLOUT Mux", "HPL Mixer", "Left HP Mixer"},
		
		/*hpr out mux*/
		{"HPROUT Mux", "HPR Mixer", "Right HP Mixer"},

		/*aux out mux*/
		{"AUXOUT Mux", "HP Mixer", "HP Mixer"},
		{"AUXOUT Mux", "SPK Mixer", "SPK Mixer"},
		{"AUXOUT Mux", "Mono Mixer", "MoNo Mixer"},

		/*spkl out pga*/
		{"SPKL Out PGA", NULL, "SPKOUT Mux"},

		/*spkr out pga*/
		{"SPKR Out PGA", NULL, "SPKOUT Mux"},
		
		/*hpl out pga*/
		{"HPL Out PGA", NULL, "HPLOUT Mux"},

		/*hpr out pga*/
		{"HPR Out PGA", NULL, "HPROUT Mux"},

		/*aux out pga*/
		{"AUX Out PGA", NULL, "AUXOUT Mux"}, 
		
		/*left adc*/
		{"Left ADC", NULL, "Left Rec Mixer"},
		
		/*right adc*/
		{"Right ADC", NULL, "Right Rec Mixer"},
		
		/*output*/
		{"SPKL", NULL, "SPKL Out PGA"},
		{"SPKR", NULL, "SPKR Out PGA"},
		{"HPL", NULL, "HPL Out PGA"},
		{"HPR", NULL, "HPR Out PGA"},
		{"AUX", NULL, "AUX Out PGA"},
};


static int rt5625_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, rt5625_dapm_widgets, 
				ARRAY_SIZE(rt5625_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	snd_soc_dapm_new_widgets(codec);

	return 0;
}

struct _pll_div{
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};


/**************************************************************
  *	watch out!
  *	our codec support you to select different source as pll input, but if you 
  *	use both of the I2S audio interface and pcm interface instantially. 
  *	The two DAI must have the same pll setting params, so you have to offer
  *	the same pll input, and set our codec's sysclk the same one, we suggest 
  *	24576000.
  **************************************************************/
static const struct _pll_div codec_master_pll1_div[] = {
		
	{   2048000,  8192000,   0x0ea0},
	{   3686400,  8192000,   0x4e27},
	{  12000000,  8192000,   0x456b},
	{  13000000,  8192000,   0x495f},
	{  13100000,  8192000,   0x0320},
	{   2048000,  11289600,	 0xf637},
	{   3686400,  11289600,  0x2f22},
	{  12000000,  11289600,  0x3e2f},
	{  13000000,  11289600,  0x4d5b},
	{  13100000,  11289600,  0x363b},
	{   2048000,  16384000,  0x1ea0},
	{   3686400,  16384000,  0x9e27},
	{  12000000,  16384000,  0x452b},
	{  13000000,  16384000,  0x542f},
	{  13100000,  16384000,  0x03a0},
	{   2048000,  16934400,  0xe625},
	{   3686400,  16934400,  0x9126},
	{  12000000,  16934400,  0x4d2c},
	{  13000000,  16934400,  0x742f},
	{  13100000,  16934400,  0x3c27},
	{   2048000,  22579200,  0x2aa0},
	{   3686400,  22579200,  0x2f20},
	{  12000000,  22579200,  0x7e2f},
	{  13000000,  22579200,  0x742f},
	{  13100000,  22579200,  0x3c27},
	{   2048000,  24576000,  0x2ea0},
	{   3686400,  24576000,  0xee27},
	{  11289600,  24576000,  0x950F},
	{  12000000,  24576000,  0x2915},
	{  12288000,  24576000,  0x0600},
	{  13000000,  24576000,  0x772e},
	{  13100000,  24576000,  0x0d20},
	{  26000000,  24576000,  0x2027},
	{  26000000,  22579200,  0x392f},
	{  24576000,  22579200,  0x0921},
	{  24576000,  24576000,  0x02a0},
};

static const struct _pll_div codec_bclk_pll1_div[] = {

	{   256000,   4096000,  0x3ea0},
	{   352800,   5644800,  0x3ea0},
	{   512000,   8192000,  0x3ea0},
	{   705600,  11289600, 	0x3ea0},
	{  1024000,  16384000,  0x3ea0},	
	{  1411200,  22579200,  0x3ea0},
	{  1536000,  24576000,  0x3ea0},	
	{  2048000,  16384000,  0x1ea0},	
	{  2822400,  22579200,  0x1ea0},
	{  3072000,  24576000,  0x1ea0},
	{   705600,  11289600, 	0x3ea0},
	{   705600,   8467200, 	0x3ab0},
	{  2822400,  11289600,  0x1ee0},
	{  3072000,  12288000,  0x1ee0},			
};

static const struct _pll_div codec_vbclk_pll1_div[] = {

	{   256000,   4096000,  0x3ea0},
	{   352800,   5644800,  0x3ea0},
	{   512000,   8192000,  0x3ea0},
	{   705600,  11289600,  0x3ea0},
	{  1024000,  16384000,  0x3ea0},	
	{  1411200,  22579200,  0x3ea0},
	{  1536000,  24576000,  0x3ea0},	
	{  2048000,  16384000,  0x1ea0},	
	{  2822400,  22579200,  0x1ea0},
	{  3072000,  24576000,  0x1ea0},
	{   705600,  11289600, 	0x3ea0},
	{   705600,   8467200, 	0x3ab0},
};


struct _coeff_div_stereo {
	unsigned int mclk;
	unsigned int rate;
	unsigned int reg60;
	unsigned int reg62;
};

struct _coeff_div_voice {
	unsigned int mclk;
	unsigned int rate;
	unsigned int reg64;
};

static const struct _coeff_div_stereo coeff_div_stereo[] = {

	/*bclk is config to 32fs, if codec is choose to be slave mode , input bclk should be 32*fs */
	{24576000,  48000,  0x3174,  0x1010},      
	{12288000,  48000,  0x1174,  0x0000},
	{18432000,  48000,  0x2174,  0x1111},
	{36864000,  48000,  0x2274,  0x2020},
	{49152000,  48000,  0xf074,  0x3030},
	{24576000,  48000,  0x3172,  0x1010},
	{24576000,   8000,  0xB274,  0x2424},
	{24576000,  16000,  0xB174,  0x2222},
	{24576000,  32000,  0xB074,  0x2121},
	{22579200,  11025,  0X3374,  0x1414},
	{22579200,  22050,  0X3274,  0x1212},
	{22579200,  44100,  0X3174,  0x1010},
	{0, 0, 0, 0},
};

static const struct _coeff_div_voice coeff_div_voice[] = {

	/*bclk is config to 32fs, if codec is choose to be slave mode , input bclk should be 32*fs */
	{24576000,  16000,  0x2622}, 
	{24576000,   8000,  0x2824},
	{0, 0, 0},
};

static int get_coeff(unsigned int mclk, unsigned int rate, int mode)
{
	int i;

	DBG("get_coeff mclk = %d, rate = %d, mode = %d\n", mclk, rate, mode);

	if (!mode) {
		for (i = 0; i < ARRAY_SIZE(coeff_div_stereo); i++) {
			if ((coeff_div_stereo[i].rate == rate) && (coeff_div_stereo[i].mclk == mclk))
				return i;
		}
	} else {
		for (i = 0; i< ARRAY_SIZE(coeff_div_voice); i++) {
			if ((coeff_div_voice[i].rate == rate) && (coeff_div_voice[i].mclk == mclk))
				return i;
		}
	}

	printk("can't find a matched mclk and rate in %s\n", 
	       (mode ? "coeff_div_voice[]" : "coeff_div_audio[]"));

	return -EINVAL;
}


static int rt5625_codec_set_dai_pll(struct snd_soc_dai *codec_dai, 
		int pll_id, unsigned int freq_in, unsigned int freq_out)
{
	int i;
	int ret = -EINVAL;
	struct snd_soc_codec *codec = codec_dai->codec;

	DBG("enter %s pll_id = %d freq_in = %d freq_out = %d\n",
	       __func__, pll_id, freq_in, freq_out);

	if (pll_id < RT5625_PLL1_FROM_MCLK || pll_id > RT5625_PLL1_FROM_VBCLK)
		return -EINVAL;

	if (!freq_in || !freq_out)
		return 0;

	if (RT5625_PLL1_FROM_MCLK == pll_id) {

		for (i = 0; i < ARRAY_SIZE(codec_master_pll1_div); i ++)
		{
			if ((freq_in == codec_master_pll1_div[i].pll_in) && (freq_out == codec_master_pll1_div[i].pll_out))
			{
				rt5625_write(codec, RT5625_GEN_CTRL_REG2, 0x0000);  /*PLL source from MCLK*/
				rt5625_write(codec, RT5625_PLL_CTRL, codec_master_pll1_div[i].regvalue);  /*set pll code*/
				rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD2, 0x8000, 0x8000);  /*enable pll1 power*/
				rt5625_write_mask(codec, RT5625_GEN_CTRL_REG1, 0x8000, 0x8000);
				ret = 0;
			}
		}
	} else if (RT5625_PLL1_FROM_BCLK == pll_id) {

		for (i = 0; i < ARRAY_SIZE(codec_bclk_pll1_div); i ++)
		{
			if ((freq_in == codec_bclk_pll1_div[i].pll_in) && (freq_out == codec_bclk_pll1_div[i].pll_out))
			{
				rt5625_write(codec, RT5625_GEN_CTRL_REG2, 0x2000);  /*PLL source from BCLK*/
				rt5625_write(codec, RT5625_PLL_CTRL, codec_bclk_pll1_div[i].regvalue);  /*set pll1 code*/
				rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD2, 0x8000, 0x8000);  /*enable pll1 power*/
				rt5625_write_mask(codec, RT5625_GEN_CTRL_REG1, 0x8000, 0x8000);
				ret = 0;
			}
		}
	} else if (RT5625_PLL1_FROM_VBCLK == pll_id) {

		for (i = 0; i < ARRAY_SIZE(codec_vbclk_pll1_div); i ++)
		{
			if ((freq_in == codec_vbclk_pll1_div[i].pll_in) && (freq_out == codec_vbclk_pll1_div[i].pll_out))
			{
				rt5625_write(codec, RT5625_GEN_CTRL_REG2, 0x3000);  /*PLL source from VBCLK*/
				rt5625_write(codec, RT5625_PLL_CTRL, codec_vbclk_pll1_div[i].regvalue);  /*set pll1 code*/
				rt5625_write_mask(codec, RT5625_PWR_MANAG_ADD2, 0x8000, 0x8000);  /*enable pll1 power*/
				rt5625_write_mask(codec, RT5625_GEN_CTRL_REG1, 0x8000, 0x8000);
				ret = 0;
			}
		}
	}
	return 0;
}


static int rt5625_hifi_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai, 
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rt5625_priv * rt5625 = codec->private_data;

	DBG("sysclk freq %u for audio i2s\n", freq);
	
	if ((freq >= (256 * 8000)) && (freq <= (512 * 48000))) {
		rt5625->stereo_sysclk = freq;
		return 0;
	}
	
	printk("unsupported sysclk freq %u for audio i2s\n", freq);
	       rt5625->stereo_sysclk=24576000;
	
	return 0;
}

static int rt5625_voice_codec_set_dai_sysclk(struct snd_soc_dai *codec_dai, 
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rt5625_priv * rt5625 = codec->private_data;

	DBG("sysclk freq %u for voice pcm\n", freq);

	if ((freq >= (256 * 8000)) && (freq <= (512 * 48000))) {
		rt5625->voice_sysclk = freq;
		return 0;
	}			

	printk("unsupported sysclk freq %u for voice pcm\n", freq);
	       rt5625->voice_sysclk = 24576000;
	
	return 0;
}


static int rt5625_hifi_pcm_hw_params(struct snd_pcm_substream *substream, 
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct rt5625_priv *rt5625 = codec->private_data;

	unsigned int iface = rt5625_read(codec, RT5625_MAIN_SDP_CTRL) & 0xfff3;
	int rate = params_rate(params);
	int coeff = get_coeff(rt5625->stereo_sysclk, rate, 0);
	
	DBG("enter %s rate = %d \n", __func__, rate);

	switch (params_format(params))
	{
		case SNDRV_PCM_FORMAT_S16_LE:
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x0004;
		case SNDRV_PCM_FORMAT_S24_LE:
			iface |= 0x0008;
		case SNDRV_PCM_FORMAT_S8:
			iface |= 0x000c;
	}

	rt5625_write(codec, RT5625_MAIN_SDP_CTRL, iface);
	rt5625_write_mask(codec, 0x3a, 0xc801, 0xc801);   /*power i2s and dac ref*/
	if (coeff >= 0) {
		rt5625_write(codec, RT5625_STEREO_DAC_CLK_CTRL1, coeff_div_stereo[coeff].reg60);
		rt5625_write(codec, RT5625_STEREO_DAC_CLK_CTRL2, coeff_div_stereo[coeff].reg62);
	}
	
	return 0;
}

static int rt5625_voice_pcm_hw_params(struct snd_pcm_substream *substream, 
			struct snd_pcm_hw_params *params,
			struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct rt5625_priv *rt5625 = codec->private_data;
	struct snd_soc_dapm_widget *w;
	unsigned int iface = rt5625_read(codec, RT5625_EXTEND_SDP_CTRL) & 0xfff3;
	int rate = params_rate(params);
	int coeff = get_coeff(rt5625->voice_sysclk, rate, 1);

	DBG("enter %s rate = %d \n", __func__, rate);

	list_for_each_entry(w, &codec->dapm_widgets, list)
	{
		if (!w->sname)
			continue;
		if (!strcmp(w->name, "Right ADC"))
			strcpy(w->sname, "Right ADC Voice Capture");
	}
	
	switch (params_format(params))
	{
		case SNDRV_PCM_FORMAT_S16_LE:
			break;
		case SNDRV_PCM_FORMAT_S20_3LE:
			iface |= 0x0004;
		case SNDRV_PCM_FORMAT_S24_LE:
			iface |= 0x0008;
		case SNDRV_PCM_FORMAT_S8:
			iface |= 0x000c;
	}
	rt5625_write_mask(codec, 0x3a, 0x0801, 0x0801);   /*power i2s and dac ref*/
	rt5625_write(codec, RT5625_EXTEND_SDP_CTRL, iface);
	if (coeff >= 0)
		rt5625_write(codec, RT5625_VOICE_DAC_PCMCLK_CTRL1, coeff_div_voice[coeff].reg64);

	return 0;
}


static int rt5625_hifi_codec_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{

	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	DBG("enter %s fmt = %d\n", __func__, fmt);

	/*set master/slave interface*/
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK)
	{
		case SND_SOC_DAIFMT_CBM_CFM:
			iface = 0x0000;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:
			iface = 0x8000;
			break;
		default:
			return -EINVAL;
	}

	/*interface format*/
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK)
	{
		case SND_SOC_DAIFMT_I2S:
			iface |= 0x0000;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			iface |= 0x0001;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			iface |= 0x0002;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			iface |= 0x0003;
			break;
		default:
			return -EINVAL;			
	}

	/*clock inversion*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK)
	{
		case SND_SOC_DAIFMT_NB_NF:
			iface |= 0x0000;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			iface |= 0x0080;
			break;
		default:
			return -EINVAL;
	}

	rt5625_write(codec, RT5625_MAIN_SDP_CTRL, iface);
	return 0;
}

static int rt5625_voice_codec_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int iface;

	DBG("enter %s\n", __func__);
	/*set slave/master mode*/
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK)
	{
		case SND_SOC_DAIFMT_CBM_CFM:
			iface = 0x0000;
			break;
		case SND_SOC_DAIFMT_CBS_CFS:
			iface = 0x4000;
			break;
		default:
			return -EINVAL;			
	}

	switch(fmt & SND_SOC_DAIFMT_FORMAT_MASK)
	{
		case SND_SOC_DAIFMT_I2S:
			iface |= 0x0000;
			break;
		case SND_SOC_DAIFMT_LEFT_J:
			iface |= 0x0001;
			break;
		case SND_SOC_DAIFMT_DSP_A:
			iface |= 0x0002;
			break;
		case SND_SOC_DAIFMT_DSP_B:
			iface |= 0x0003;
			break;
		default:
			return -EINVAL;		
	}

	/*clock inversion*/
	switch (fmt & SND_SOC_DAIFMT_INV_MASK)
	{
		case SND_SOC_DAIFMT_NB_NF:
			iface |= 0x0000;
			break;
		case SND_SOC_DAIFMT_IB_NF:
			iface |= 0x0080;
			break;
		default:
			return -EINVAL;			
	}

	iface |= 0x8000;      /*enable vopcm*/
	rt5625_write(codec, RT5625_EXTEND_SDP_CTRL, iface);	
	return 0;
}


static int rt5625_hifi_codec_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	if (mute) 
		rt5625_write_mask(codec, RT5625_STEREO_DAC_VOL, 0x8080, 0x8080);
	else
		rt5625_write_mask(codec, RT5625_STEREO_DAC_VOL, 0x0000, 0x8080);
	return 0;
}

static int rt5625_voice_codec_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	if (mute)
		rt5625_write_mask(codec, RT5625_VOICE_DAC_OUT_VOL, 0x1000, 0x1000);
	else 
		rt5625_write_mask(codec, RT5625_VOICE_DAC_OUT_VOL, 0x0000, 0x1000);
	return 0;
}


static int rt5625_set_bias_level(struct snd_soc_codec *codec, 
			enum snd_soc_bias_level level)
{
	switch(level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		rt5625_write(codec, 0x26, 0x0000);
		rt5625_write_mask(codec, 0x3c, 0x2000, 0x2000);     
		rt5625_write_mask(codec, 0x3a, 0x000e, 0x000e);        	
		break;
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		rt5625_write_mask(codec, 0x04, 0x8080, 0x8080);        /*mute hp*/
		rt5625_write_mask(codec, 0x02, 0x8080, 0x8080);        /*mute spk*/
		rt5625_write(codec, 0x3e, 0x0000);					//power off all bit
		rt5625_write(codec, 0x3a, 0x0000);					//power off all bit
		rt5625_write(codec, 0x3c, 0x0000);					//power off all bit
		
		break;
	}
	codec->bias_level = level;
	return 0;
}


#define RT5625_STEREO_RATES	SNDRV_PCM_RATE_8000_48000

#define RT5626_VOICE_RATES SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_8000

#define RT5625_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE |\
			SNDRV_PCM_FMTBIT_S8)

static struct snd_soc_dai_ops rt5625_dai_ops_hifi = {

	.hw_params	= rt5625_hifi_pcm_hw_params,
//	.digital_mute	= rt5625_hifi_codec_mute,
	.set_fmt	= rt5625_hifi_codec_set_dai_fmt,
	.set_pll	= rt5625_codec_set_dai_pll,
	.set_sysclk	= rt5625_hifi_codec_set_dai_sysclk,

};


static struct snd_soc_dai_ops rt5625_dai_ops_voice = {

	.hw_params	= rt5625_voice_pcm_hw_params,
//	.digital_mute	= rt5625_voice_codec_mute,
	.set_fmt	= rt5625_voice_codec_set_dai_fmt,
	.set_pll	= rt5625_codec_set_dai_pll,
	.set_sysclk	= rt5625_voice_codec_set_dai_sysclk,

};



struct snd_soc_dai rt5625_dai[] = {
	/*hifi codec dai*/
	{
		.name = "RT5625 HiFi",
		.id = 1,
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5625_STEREO_RATES,
			.formats = RT5625_FORMATS,
		},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5625_STEREO_RATES,
			.formats = RT5625_FORMATS,
		},
		
		.ops = &rt5625_dai_ops_hifi,
	},

	/*voice codec dai*/
	{
		.name = "RT5625 Voice",
		.id = 2,
		.playback = {
			.stream_name = "Voice Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = RT5626_VOICE_RATES,
			.formats = RT5625_FORMATS,
		},
		.capture = {
			.stream_name = "Voice Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = RT5626_VOICE_RATES,
			.formats = RT5625_FORMATS,
		},
		
		.ops = &rt5625_dai_ops_voice,

	},
};

EXPORT_SYMBOL_GPL(rt5625_dai);


static void rt5625_work(struct work_struct *work)
{
	struct snd_soc_codec *codec =
		 container_of(work, struct snd_soc_codec, delayed_work.work);
	rt5625_set_bias_level(codec, codec->bias_level);
}


#if defined(CONFIG_SND_HWDEP)

#if REALTEK_HWDEP

#define RT_CE_CODEC_HWDEP_NAME "rt56xx hwdep "


static int rt56xx_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	DBG("enter %s\n", __func__);
	return 0;
}

static int rt56xx_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	DBG("enter %s\n", __func__);
	return 0;
}


static int rt56xx_hwdep_ioctl_common(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rt56xx_cmd rt56xx;
	struct rt56xx_cmd __user *_rt56xx = arg;
	struct rt56xx_reg_state *buf;
	struct rt56xx_reg_state *p;
	struct snd_soc_codec *codec = hw->private_data;

	if (copy_from_user(&rt56xx, _rt56xx, sizeof(rt56xx)))
		return -EFAULT;
	buf = kmalloc(sizeof(*buf) * rt56xx.number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	if (copy_from_user(buf, rt56xx.buf, sizeof(*buf) * rt56xx.number)) {
		goto err;
	}
	switch (cmd) {
		case RT_READ_CODEC_REG_IOCTL:
			for (p = buf; p < buf + rt56xx.number; p++)
			{
				p->reg_value = codec->read(codec, p->reg_index);
			}
			if (copy_to_user(rt56xx.buf, buf, sizeof(*buf) * rt56xx.number))
				goto err;
				
			break;
		case RT_WRITE_CODEC_REG_IOCTL:
			for (p = buf; p < buf + rt56xx.number; p++)
				codec->write(codec, p->reg_index, p->reg_value);
			break;
	}

	kfree(buf);
	return 0;

err:
	kfree(buf);
	return -EFAULT;
	
}

static int rt56xx_codec_dump_reg(struct snd_hwdep *hw, struct file *file, unsigned long arg)
{
	struct rt56xx_cmd rt56xx;
	struct rt56xx_cmd __user *_rt56xx = arg;
	struct rt56xx_reg_state *buf;
	struct snd_soc_codec *codec = hw->private_data;
	int number = codec->reg_cache_size;
	int i;

	DBG("enter %s, number = %d\n", __func__, number);	
	if (copy_from_user(&rt56xx, _rt56xx, sizeof(rt56xx)))
		return -EFAULT;
	
	buf = kmalloc(sizeof(*buf) * number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	for (i = 0; i < number; i++)
	{
		buf[i].reg_index = i << 1;
		buf[i].reg_value = codec->read(codec, buf[i].reg_index);
	}
	if (copy_to_user(rt56xx.buf, buf, sizeof(*buf) * i))
		goto err;
	rt56xx.number = number;
	if (copy_to_user(_rt56xx, &rt56xx, sizeof(rt56xx)))
		goto err;
	kfree(buf);
	return 0;
err:
	kfree(buf);
	return -EFAULT;
	
}

static int rt56xx_hwdep_ioctl(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == RT_READ_ALL_CODEC_REG_IOCTL)
	{
		return rt56xx_codec_dump_reg(hw, file, arg);
	}
	else
	{
		return rt56xx_hwdep_ioctl_common(hw, file, cmd, arg);
	}
}

int realtek_ce_init_hwdep(struct snd_soc_codec *codec)
{
	struct snd_hwdep *hw;
	struct snd_card *card = codec->card;
	int err;

	if ((err = snd_hwdep_new(card, RT_CE_CODEC_HWDEP_NAME, 0, &hw)) < 0)
		return err;
	
	strcpy(hw->name, RT_CE_CODEC_HWDEP_NAME);
	hw->private_data = codec;
	hw->ops.open = rt56xx_hwdep_open;
	hw->ops.release = rt56xx_hwdep_release;
	hw->ops.ioctl = rt56xx_hwdep_ioctl;
	return 0;
}

#endif

#endif

static int rt5625_init(struct snd_soc_device *socdev)
{

	struct snd_soc_codec *codec = socdev->card->codec;
	int ret = 0;

	codec->name = "RT5625";
	codec->owner = THIS_MODULE;
	codec->read = rt5625_read;
	codec->write = rt5625_write;
	codec->set_bias_level = rt5625_set_bias_level;
	codec->dai= rt5625_dai;
	codec->num_dai = 2;
	codec->reg_cache_step = 2;		
	codec->reg_cache_size = ARRAY_SIZE(rt5625_reg)*2;
	codec->reg_cache = kmemdup(rt5625_reg, sizeof(rt5625_reg), GFP_KERNEL);
	if (codec->reg_cache == NULL)
		return -ENOMEM;

	rt5625_reset(codec);

	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0 )
	{
		printk(KERN_ERR "rt5625:  failed to create pcms\n");
		goto pcm_err;
	}
	
	rt5625_write(codec, RT5625_PD_CTRL_STAT, 0);
	rt5625_write(codec, RT5625_PWR_MANAG_ADD1, PWR_MAIN_BIAS);
	rt5625_write(codec, RT5625_PWR_MANAG_ADD2, PWR_MIXER_VREF);
	rt5625_reg_init(codec);
	rt5625_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	codec->bias_level = SND_SOC_BIAS_STANDBY;
	schedule_delayed_work(&codec->delayed_work, msecs_to_jiffies(80));

#if (RT5625_EQ_FUNC_ENA==1)	
	rt5625_update_eqmode(codec,POP);
#endif
	
	rt5625_add_controls(codec);
	rt5625_add_widgets(codec);

	#if defined(CONFIG_SND_HWDEP)

		#if REALTEK_HWDEP

		 realtek_ce_init_hwdep(codec);

		#endif

	#endif

	ret = snd_soc_init_card(socdev);

	if (ret < 0)
	{
		printk(KERN_ERR "rt5625: failed to register card\n");
		goto card_err;
	}
	DBG("rt5625: initial ok\n");
	return 0;

	card_err:
		snd_soc_free_pcms(socdev);
		snd_soc_dapm_free(socdev);
	
	pcm_err:
		kfree(codec->reg_cache);
		return ret;
	
	
}


static int rt5625_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = rt5625_socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	int ret;

	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = rt5625_init(socdev);
	if (ret < 0)
		pr_err("failed to initialise rt5625\n");

	return ret;
}

static int rt5625_i2c_remove(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	kfree(codec->reg_cache);
	return 0;
}

static const struct i2c_device_id rt5625_i2c_id[] = {
		{"rt5625", 0},
		{}
};
MODULE_DEVICE_TABLE(i2c, rt5625_i2c_id);
static struct i2c_driver rt5625_i2c_driver = {
	.driver = {
		.name = "RT5625 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    rt5625_i2c_probe,
	.remove =   rt5625_i2c_remove,
	.id_table = rt5625_i2c_id,
};


static int rt5625_add_i2c_device(struct platform_device *pdev,
				 const struct rt5625_setup_data *setup)
{
#if 0
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
#endif
	int ret;

	ret = i2c_add_driver(&rt5625_i2c_driver);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't add i2c driver\n");
		return ret;
	}
#if 0
	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = setup->i2c_address;
	strlcpy(info.type, "rt5625", I2C_NAME_SIZE);

	adapter = i2c_get_adapter(setup->i2c_bus);
	if (!adapter) {
		dev_err(&pdev->dev, "can't get i2c adapter %d\n",
			setup->i2c_bus);
		goto err_driver;
	}

	client = i2c_new_device(adapter, &info);
	i2c_put_adapter(adapter);
	if (!client) {
		dev_err(&pdev->dev, "can't add i2c device at 0x%x\n",
			(unsigned int)info.addr);
		goto err_driver;
	}
#endif
	return 0;
#if 0
err_driver:
	i2c_del_driver(&rt5625_i2c_driver);
	return -ENODEV;
#endif
}


static int rt5625_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct rt5625_setup_data *setup;
	struct snd_soc_codec *codec;
	struct rt5625_priv *rt5625;
	int ret;

	pr_info("RT5625 Audio Codec %s", RT5625_VERSION);

	if(socdev->codec_data)
	{
		setup = socdev->codec_data;		
	}

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	rt5625 = kzalloc(sizeof(struct rt5625_priv), GFP_KERNEL);
	if (rt5625 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}
	codec->private_data = rt5625;
	socdev->card->codec = codec;

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	rt5625_socdev = socdev;
	INIT_DELAYED_WORK(&codec->delayed_work, rt5625_work);

	ret = -ENODEV;
//	if (setup->i2c_address) 
	{
		codec->hw_write = (hw_write_t)i2c_master_send;
		//codec->hw_read = (hw_read_t)i2c_master_recv;
		ret = rt5625_add_i2c_device(pdev, setup);
	}

	if (ret != 0) {
		kfree(codec->private_data);
		kfree(codec);
		socdev->card->codec = NULL;
	}
	return ret;
}

static int run_delayed_work(struct delayed_work *dwork)
{
	int ret;

	/* cancel any work waiting to be queued. */
	ret = cancel_delayed_work(dwork);

	/* if there was any work waiting then we run it now and
	 * wait for it's completion */
	if (ret) {
		schedule_delayed_work(dwork, 0);
		flush_scheduled_work();
	}
	return ret;
}


static int rt5625_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	if (codec->control_data)
		rt5625_set_bias_level(codec, SND_SOC_BIAS_OFF);
	run_delayed_work(&codec->delayed_work);
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
	i2c_unregister_device(codec->control_data);
	i2c_del_driver(&rt5625_i2c_driver);
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}


static int rt5625_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	rt5625_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int rt5625_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	
//	int i;
//	u8 data[3];
//	u16 *cache = codec->reg_cache;

#if 1
	rt5625_reset(codec);
	rt5625_write(codec, RT5625_PD_CTRL_STAT, 0);
	rt5625_write(codec, RT5625_PWR_MANAG_ADD1, PWR_MAIN_BIAS);
	rt5625_write(codec, RT5625_PWR_MANAG_ADD2, PWR_MIXER_VREF);
	rt5625_reg_init(codec);
#else
	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(rt5625_reg); i++) {
		if (i == RT5625_RESET)
			continue;
		data[0] = i << 1;
		data[1] = (0xff00 & cache[i]) >> 8;
		data[2] = 0x00ff & cache[i];
		codec->hw_write(codec->control_data, data, 3);
	}

	rt5625_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
#endif

	/* charge rt5625 caps */
	if (codec->suspend_bias_level == SND_SOC_BIAS_ON) {
		rt5625_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
		codec->bias_level = SND_SOC_BIAS_ON;
		schedule_delayed_work(&codec->delayed_work,
					msecs_to_jiffies(100));
	}

	return 0;
}


struct snd_soc_codec_device soc_codec_dev_rt5625 = {
	.probe = 	rt5625_probe,
	.remove = 	rt5625_remove,
	.suspend = 	rt5625_suspend,
	.resume =	rt5625_resume,
};

EXPORT_SYMBOL_GPL(soc_codec_dev_rt5625);

static int __init rt5625_modinit(void)
{
	return snd_soc_register_dais(rt5625_dai, ARRAY_SIZE(rt5625_dai));
}

static void __exit rt5625_exit(void)
{
	snd_soc_unregister_dais(rt5625_dai, ARRAY_SIZE(rt5625_dai));
}

module_init(rt5625_modinit);
module_exit(rt5625_exit);
MODULE_LICENSE("GPL");

