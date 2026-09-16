/* Resource IDs shared by the .rc script and platform_win32.c */
#ifndef BH_RESOURCE_H
#define BH_RESOURCE_H

#define IDS_DESCRIPTION   1      /* Windows shows string 1 as the screensaver name */
#define IDI_APP           1      /* first icon: Explorer and the screensaver list */
#define IDD_CONFIG        101

#define IDC_STATIC        -1

/* Motion and camera */
#define IDC_SIDE          1001
#define IDC_SPEED_LBL     1002
#define IDC_SPEED         1003
#define IDC_SPEED_VAL     1004
#define IDC_MOUSEROT      1005
#define IDC_EXITMOUSE     1006
#define IDC_SENS_LBL      1007
#define IDC_SENS          1008
#define IDC_SENS_VAL      1009
#define IDC_CLICKRESET    1010
#define IDC_EXITKEY       1011
#define IDC_ROT360        1012
#define IDC_ROTMODE_LBL   1013
#define IDC_ROT_ORBIT     1014
#define IDC_ROT_YAW       1015
#define IDC_ROT_TUMBLE    1016
#define IDC_ROTSEC_LBL    1017
#define IDC_ROTSEC        1018
#define IDC_ROTSEC_VAL    1019
#define IDC_WARP          1022
#define IDC_WARP_VAL      1023

/* Particles */
#define IDC_STARS         1030
#define IDC_STARS_VAL     1031
#define IDC_PSIZE         1032
#define IDC_PSIZE_VAL     1033
#define IDC_GHOST         1034
#define IDC_GHOST_VAL     1035
#define IDC_BLUR          1036
#define IDC_BLUR_VAL      1037
#define IDC_TWINKLE       1038
#define IDC_TWINKLE_VAL   1039
#define IDC_NATURAL       1040

/* Colours */
#define IDC_STARCOL       1050
#define IDC_SPACECOL      1051
#define IDC_DISKCOL       1052
#define IDC_RINGCOL       1053
#define IDC_SHADOWCOL     1054
#define IDC_NEBULACOL     1055

/* Physics and events */
#define IDC_SPIN          1060
#define IDC_SPIN_VAL      1061
#define IDC_LENSING       1062
#define IDC_LENSING_VAL   1063
#define IDC_DOPPLER       1064
#define IDC_REDSHIFT      1065
#define IDC_HIGHORDER     1066
#define IDC_TIMEDIL       1067
#define IDC_TIMEDIL_VAL   1068
#define IDC_JETS          1069
#define IDC_EV_MERGER     1070
#define IDC_EV_FALLIN     1071
#define IDC_EV_FLARE      1072

/* Camera damage */
#define IDC_DAMAGE        1080
#define IDC_DMG_GRACE_LBL 1081
#define IDC_DMG_GRACE     1082
#define IDC_DMG_GRACE_VAL 1083
#define IDC_DMG_HEAL_LBL  1084
#define IDC_DMG_HEAL      1085
#define IDC_DMG_HEAL_VAL  1086
#define IDC_DMG_GLASS     1087
#define IDC_DMG_MATRIX    1088
#define IDC_DMG_PAL_LBL   1089
#define IDC_DMG_WHITE     1090
#define IDC_DMG_GREEN     1091
#define IDC_DMG_BLACK     1092
#define IDC_DMG_CUSTOM    1093
#define IDC_DMGCOL        1094

/* Power */
#define IDC_QUALITY       1100
#define IDC_QUALITY_VAL   1101
#define IDC_FPS           1102
#define IDC_FPS_VAL       1103

/* Scenes: the hidden section, revealed by Ctrl+Alt+S inside the dialog.
 * SCENE_COUNT consecutive IDs, one checkbox each. */
#define IDC_SCENE_BOX     1110
#define IDC_SCENE_FIRST   1111
#define IDC_SCENE_LAST    (IDC_SCENE_FIRST + 9)
#define IDC_SCENE_ALL     1130
#define IDC_SCENE_NONE    1131

#define IDC_DEFAULTS      1140
#define IDC_PREVIEW       1141

#endif
