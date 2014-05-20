/*
	NatFeat host CD-ROM access, Linux CD-ROM driver

	ARAnyM (C) 2003-2005 Patrice Mandin

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <SDL_cdrom.h>
#include <SDL_endian.h>

#include <linux/cdrom.h>
#include <errno.h>
#include "toserror.h"

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "parameters.h"
#include "nfcdrom.h"
#include "nfcdrom_atari.h"
#include "nfcdrom_linux.h"

#define DEBUG 0
#include "debug.h"

/*--- Defines ---*/

#define NFCD_NAME	"nf:cdrom:linux: "

/*--- Public functions ---*/

CdromDriverLinux::CdromDriverLinux()
{
	int i;

	D(bug(NFCD_NAME "CdromDriverLinux()"));
	for (i=0; i<CD_MAX_DRIVES; i++) {
		drive_handles[i]=-1;
	}
}

CdromDriverLinux::~CdromDriverLinux()
{
	D(bug(NFCD_NAME "~CdromDriverLinux()"));
}

const char *CdromDriverLinux::DeviceName(int drive)
{
	return SDL_CDName(drive);
}

/*--- Private functions ---*/

int CdromDriverLinux::OpenDrive(memptr device)
{
	int drive;

	drive = GetDrive(device);
	drive = DriveFromLetter(drive);
	
	/* Drive exist ? */
	if (drive < 0 || drive >= CD_MAX_DRIVES || (drives_mask & (1<<drive))==0) {
		D(bug(NFCD_NAME " physical device %c does not exist", GetDrive(device)));
		return TOS_EUNDEV;
	}

	/* Drive opened ? */
	if (drive_handles[drive]>=0) {
		return drive;
	}

	/* Open drive */
	drive_handles[drive]=open(DeviceName(bx_options.nfcdroms[drive].physdevtohostdev), O_RDONLY|O_EXCL|O_NONBLOCK, 0);
	if (drive_handles[drive]<0) {
		drive_handles[drive]=-1;
		int errorcode = errnoHost2Mint(errno, TOS_EFILNF);
		D(bug(NFCD_NAME " error opening drive %s: %s", DeviceName(bx_options.nfcdroms[drive].physdevtohostdev), strerror(errno)));
		return errorcode;
	}

	return drive;
}

void CdromDriverLinux::CloseDrive(int drive)
{
	/* Drive already closed ? */
	if (drive_handles[drive]<0) {
		return;
	}

	close(drive_handles[drive]);
	drive_handles[drive]=-1;
}

int CdromDriverLinux::cd_unixioctl(int drive, int request, void *buffer)
{
	int errorcode = ioctl(drive_handles[drive], request, buffer);
	if (errorcode >= 0)
		return errorcode;
	return errnoHost2Mint(errno, TOS_EDRVNR);
}

int32 CdromDriverLinux::cd_read(memptr device, memptr buffer, uint32 first, uint32 length)
{
	int drive;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	D(bug(NFCD_NAME "Read(%d,%d)", first, length));

	if (lseek(drive_handles[drive], (off_t)first * CD_FRAMESIZE, SEEK_SET)<0) {
		D(bug(NFCD_NAME "Read(): can not seek to block %d", first));
		int errorcode = errnoHost2Mint(errno, TOS_ENOSYS);
		CloseDrive(drive);
		return errorcode;
	}

	if (read(drive_handles[drive], Atari2HostAddr(buffer), length * CD_FRAMESIZE)<0) {
		int errorcode = errnoHost2Mint(errno, TOS_ENOSYS);
		D(bug(NFCD_NAME "Read(): can not read %d blocks", length));
		CloseDrive(drive);
		return errorcode;
	}

	CloseDrive(drive);
	return TOS_E_OK;
}

int32 CdromDriverLinux::cd_status(memptr device, memptr ext_status)
{
	UNUSED(ext_status);
	int drive, errorcode, mediachanged;
	unsigned long status;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	status = 0;
	errorcode=cd_unixioctl(drive, CDROM_MEDIA_CHANGED, &status);
	D(bug(NFCD_NAME "Status(CDROM_MEDIA_CHANGED): errorcode=0x%08x", errorcode));
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}
	mediachanged = (errorcode==1);

	status = 0;
	errorcode=cd_unixioctl(drive, CDROM_DRIVE_STATUS, &status);
	D(bug(NFCD_NAME "Status(CDROM_DRIVE_STATUS): errorcode=0x%08x", errorcode));
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}
	if (errorcode == CDS_DRIVE_NOT_READY) {
		CloseDrive(drive);
		return TOS_EDRVNR;
	}

	CloseDrive(drive);
	return (mediachanged<<3);
}

int32 CdromDriverLinux::cd_ioctl(memptr device, uint16 opcode, memptr buffer)
{
	int drive, errorcode = TOS_ENOSYS;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	switch(opcode) {
		case ATARI_CDROMREADOFFSET:
			{
				struct cdrom_multisession cd_ms;
				Uint32 *lba = (Uint32 *)Atari2HostAddr(buffer);
				D(bug(NFCD_NAME " Ioctl(CDROMREADOFFSET)"));
				cd_ms.xa_flag = 1;
				cd_ms.addr_format = CDROM_LBA;
				errorcode = cd_unixioctl(drive, CDROMMULTISESSION, &cd_ms);
				if (errorcode >= 0)
				{
					if (cd_ms.addr_format == CDROM_LBA)
						*lba = SDL_SwapBE32(cd_ms.addr.lba);
					else
						*lba = SDL_SwapBE32(MSF_TO_FRAMES(cd_ms.addr.msf.minute, cd_ms.addr.msf.second, cd_ms.addr.msf.frame));
				}
			}
			break;
		
		case ATARI_CDROMPAUSE:
			D(bug(NFCD_NAME " Ioctl(CDROMPAUSE)"));
			errorcode = cd_unixioctl(drive, CDROMPAUSE, 0l);
			break;
			
		case ATARI_CDROMRESUME:
			D(bug(NFCD_NAME " Ioctl(CDROMRESUME)"));
			errorcode = cd_unixioctl(drive, CDROMRESUME, 0l);
			break;
			
		case ATARI_CDROMPLAYMSF:
			{
				struct cdrom_msf cd_msf;
				atari_cdrom_msf_t *atari_msf;
				
				atari_msf = (atari_cdrom_msf_t *) Atari2HostAddr(buffer);
				cd_msf.cdmsf_min0 = atari_msf->cdmsf_min0;
				cd_msf.cdmsf_sec0 = atari_msf->cdmsf_sec0;
				cd_msf.cdmsf_frame0 = atari_msf->cdmsf_frame0;
				cd_msf.cdmsf_min1 = atari_msf->cdmsf_min1;
				cd_msf.cdmsf_sec1 = atari_msf->cdmsf_sec1;
				cd_msf.cdmsf_frame1 = atari_msf->cdmsf_frame1;

				D(bug(NFCD_NAME " Ioctl(CDROMPLAYMSF,%02d:%02d:%02d,%02d:%02d:%02d)",
					cd_msf.cdmsf_min0, cd_msf.cdmsf_sec0, cd_msf.cdmsf_frame0,
					cd_msf.cdmsf_min1, cd_msf.cdmsf_sec1, cd_msf.cdmsf_frame1
				));

				errorcode=cd_unixioctl(drive, CDROMPLAYMSF, &cd_msf);
			}
			break;

		case ATARI_CDROMPLAYTRKIND:
			{
				struct cdrom_ti cd_ti;
				atari_cdrom_ti *atari_ti;
				
				atari_ti = (atari_cdrom_ti *) Atari2HostAddr(buffer);
				cd_ti.cdti_trk0 = atari_ti->cdti_trk0;
				cd_ti.cdti_ind0 = atari_ti->cdti_ind0;
				cd_ti.cdti_trk1 = atari_ti->cdti_trk1;
				cd_ti.cdti_ind1 = atari_ti->cdti_ind1;

				D(bug(NFCD_NAME " Ioctl(CDROMPLAYTRKIND,%d.%d,%d.%d)",
					cd_ti.cdti_trk0, cd_ti.cdti_ind0,
					cd_ti.cdti_trk1, cd_ti.cdti_trk1
				));

				errorcode=cd_unixioctl(drive, CDROMPLAYTRKIND, &cd_ti);
			}
			break;

		case ATARI_CDROMREADTOCHDR:
			{
				struct cdrom_tochdr tochdr;
				atari_cdromtochdr_t *atari_tochdr;
				
				atari_tochdr = (atari_cdromtochdr_t *)  Atari2HostAddr(buffer);
				D(bug(NFCD_NAME " Ioctl(CDROMREADTOCHDR)"));
				errorcode = cd_unixioctl(drive, CDROMREADTOCHDR, &tochdr);
				if (errorcode >= 0)
				{
					atari_tochdr->cdth_trk0 = tochdr.cdth_trk0;
					atari_tochdr->cdth_trk1 = tochdr.cdth_trk1;
				}
			}
			break;
			
		case ATARI_CDROMREADTOCENTRY:
			{
				struct cdrom_tocentry tocentry;
				atari_cdromtocentry_t *atari_tocentry;
				
				atari_tocentry = (atari_cdromtocentry_t *)  Atari2HostAddr(buffer);
				tocentry.cdte_track = atari_tocentry->cdte_track;
				tocentry.cdte_format = atari_tocentry->cdte_format;

				D(bug(NFCD_NAME " Ioctl(CDROMREADTOCENTRY,0x%02x,0x%02x)", tocentry.cdte_track, tocentry.cdte_format));
				errorcode=cd_unixioctl(drive, CDROMREADTOCENTRY, &tocentry);

				if (errorcode>=0) {
					atari_tocentry->cdte_track = tocentry.cdte_track;
					atari_tocentry->cdte_format = tocentry.cdte_format;
					atari_tocentry->cdte_info = (tocentry.cdte_adr & 0x0f)<<4;
					atari_tocentry->cdte_info |= tocentry.cdte_ctrl & 0x0f;
					atari_tocentry->cdte_datamode = tocentry.cdte_datamode;
					atari_tocentry->dummy = 0;

					if (tocentry.cdte_format == CDROM_LBA) {
						atari_tocentry->cdte_addr = SDL_SwapBE32(tocentry.cdte_addr.lba);
					} else {
						atari_tocentry->cdte_addr = SDL_SwapBE32(((Uint32)tocentry.cdte_addr.msf.minute<<16) | ((Uint32)tocentry.cdte_addr.msf.second<<8) | (Uint32)tocentry.cdte_addr.msf.frame);
					}
				}
			}
			break;

		case ATARI_CDROMSTOP:
			D(bug(NFCD_NAME " Ioctl(CDROMSTOP)"));
			errorcode = cd_unixioctl(drive, CDROMSTOP, 0l);
			break;
			
		case ATARI_CDROMSTART:
			D(bug(NFCD_NAME " Ioctl(CDROMSTART)"));
			errorcode = cd_unixioctl(drive, CDROMSTART, 0l);
			break;
			
		case ATARI_CDROMEJECT:
			D(bug(NFCD_NAME " Ioctl(CDROMEJECT)"));
			errorcode=cd_unixioctl(drive, buffer != 0 ? CDROMCLOSETRAY : CDROMEJECT, (void *)0);
			break;

		case ATARI_CDROMVOLCTRL:
			{
				struct cdrom_volctrl volctrl;
				struct atari_cdrom_volctrl *v = (struct atari_cdrom_volctrl *) Atari2HostAddr(buffer);
	
				D(bug(NFCD_NAME " Ioctl(CDROMVOLCTRL)"));

				/* Write volume settings */
				volctrl.channel0 = v->channel0;
				volctrl.channel1 = v->channel1;
				volctrl.channel2 = v->channel2;
				volctrl.channel3 = v->channel3;

				errorcode=cd_unixioctl(drive, CDROMVOLCTRL, &volctrl);
			}
			break;
				
		case ATARI_CDROMAUDIOCTRL:
			{
				struct cdrom_volctrl volctrl;
				atari_cdrom_audioctrl_t *atari_audioctrl;
				atari_audioctrl = (atari_cdrom_audioctrl_t *) Atari2HostAddr(buffer);

				D(bug(NFCD_NAME " Ioctl(CDROMAUDIOCTRL,0x%04x)", atari_audioctrl->set));

				if (atari_audioctrl->set == 0) {
					/* Read volume settings */
					errorcode=cd_unixioctl(drive, CDROMVOLREAD, &volctrl);
					if (errorcode>=0) {
						atari_audioctrl->channel[0].selection =
							atari_audioctrl->channel[1].selection =
							atari_audioctrl->channel[2].selection =
							atari_audioctrl->channel[3].selection = 0;
						atari_audioctrl->channel[0].volume = volctrl.channel0;
						atari_audioctrl->channel[1].volume = volctrl.channel1;
						atari_audioctrl->channel[2].volume = volctrl.channel2;
						atari_audioctrl->channel[3].volume = volctrl.channel3;
					}
				} else {
					/* Write volume settings */
					volctrl.channel0 = atari_audioctrl->channel[0].volume;
					volctrl.channel1 = atari_audioctrl->channel[1].volume;
					volctrl.channel2 = atari_audioctrl->channel[2].volume;
					volctrl.channel3 = atari_audioctrl->channel[3].volume;

					errorcode=cd_unixioctl(drive, CDROMVOLCTRL, &volctrl);
				}
			}
			break;

		case ATARI_CDROMSUBCHNL:
			{
				/* Structure is different between Atari and Linux */
				struct cdrom_subchnl subchnl;
				atari_cdromsubchnl_t *atari_subchnl;
				
				atari_subchnl = (atari_cdromsubchnl_t *) Atari2HostAddr(buffer);

				subchnl.cdsc_format = atari_subchnl->cdsc_format;

				D(bug(NFCD_NAME " Ioctl(READSUBCHNL,0x%02x)", atari_subchnl->cdsc_format));

				errorcode=cd_unixioctl(drive, CDROMSUBCHNL, &subchnl);

				if (errorcode>=0) {
					atari_subchnl->cdsc_format = subchnl.cdsc_format;
					atari_subchnl->cdsc_audiostatus = subchnl.cdsc_audiostatus;
					atari_subchnl->cdsc_resvd = 0;
					atari_subchnl->cdsc_info = (subchnl.cdsc_adr & 0x0f)<<4;
					atari_subchnl->cdsc_info |= subchnl.cdsc_ctrl & 0x0f;
					atari_subchnl->cdsc_trk = subchnl.cdsc_trk;
					atari_subchnl->cdsc_ind = subchnl.cdsc_ind;

					if (subchnl.cdsc_format == CDROM_LBA) {
						atari_subchnl->cdsc_absaddr = SDL_SwapBE32(subchnl.cdsc_absaddr.lba);
						atari_subchnl->cdsc_reladdr = SDL_SwapBE32(subchnl.cdsc_reladdr.lba);
					} else {
						atari_subchnl->cdsc_absaddr = SDL_SwapBE32(subchnl.cdsc_absaddr.msf.minute<<16);
						atari_subchnl->cdsc_absaddr |= SDL_SwapBE32(subchnl.cdsc_absaddr.msf.second<<8);
						atari_subchnl->cdsc_absaddr |= SDL_SwapBE32(subchnl.cdsc_absaddr.msf.frame);

						atari_subchnl->cdsc_reladdr = SDL_SwapBE32(subchnl.cdsc_reladdr.msf.minute<<16);
						atari_subchnl->cdsc_reladdr |= SDL_SwapBE32(subchnl.cdsc_reladdr.msf.second<<8);
						atari_subchnl->cdsc_reladdr |= SDL_SwapBE32(subchnl.cdsc_reladdr.msf.frame);
					}
				}
			}
			break;

		case ATARI_CDROMREADMODE2:
		case ATARI_CDROMREADMODE1:
			{
				struct cdrom_read cd_read;
				atari_cdrom_read_t *atari_cdread;
				
				atari_cdread = (atari_cdrom_read_t *) Atari2HostAddr(buffer);
				cd_read.cdread_lba = SDL_SwapBE32(atari_cdread->cdread_lba);
				cd_read.cdread_bufaddr = (char *)Atari2HostAddr(SDL_SwapBE32(atari_cdread->cdread_bufaddr));
				cd_read.cdread_buflen = SDL_SwapBE32(atari_cdread->cdread_buflen);

				D(bug(NFCD_NAME " Ioctl(%s)", opcode == ATARI_CDROMREADMODE1 ? "CDROMREADMODE1" : "CDROMREADMODE2"));
				errorcode=cd_unixioctl(drive, opcode == ATARI_CDROMREADMODE1 ? CDROMREADMODE1 : CDROMREADMODE2, &cd_read);
			}
			break;

		case ATARI_CDROMPREVENTREMOVAL:
			D(bug(NFCD_NAME " Ioctl(CDROMPREVENTREMOVAL)"));
			errorcode=cd_unixioctl(drive, CDROM_LOCKDOOR, (void *)1);
			break;

		case ATARI_CDROMALLOWREMOVAL:
			D(bug(NFCD_NAME " Ioctl(CDROMALLOWREMOVAL)"));
			errorcode=cd_unixioctl(drive, CDROM_LOCKDOOR, (void *)0);
			break;

		case ATARI_CDROMREADDA:
			{
				struct cdrom_msf cd_msf;
				atari_cdrom_msf_t *atari_msf;
				
				atari_msf = (atari_cdrom_msf_t *) Atari2HostAddr(buffer);
				cd_msf.cdmsf_min0 = atari_msf->cdmsf_min0;
				cd_msf.cdmsf_sec0 = atari_msf->cdmsf_sec0;
				cd_msf.cdmsf_frame0 = atari_msf->cdmsf_frame0;
				cd_msf.cdmsf_min1 = atari_msf->cdmsf_min1;
				cd_msf.cdmsf_sec1 = atari_msf->cdmsf_sec1;
				cd_msf.cdmsf_frame1 = atari_msf->cdmsf_frame1;
				D(bug(NFCD_NAME " Ioctl(CDROMREADDA)"));
				errorcode=cd_unixioctl(drive, CDROMREADRAW, &cd_msf);
			}
			break;

		case ATARI_CDROMRESET:
			D(bug(NFCD_NAME " Ioctl(CDROMRESET)"));
			break;
	
		case ATARI_CDROMGETMCN:
			{
				atari_mcn_t	*atari_mcn;
				cdrom_mcn mcn;
				
				atari_mcn = (atari_mcn_t *) Atari2HostAddr(buffer);
				memset(atari_mcn, 0, sizeof(atari_mcn_t));

				D(bug(NFCD_NAME " Ioctl(CDROMGETMCN)"));
				errorcode=cd_unixioctl(drive, CDROM_GET_MCN, &mcn);
				if (errorcode >= 0)
					Host2AtariSafeStrncpy(buffer + offsetof(atari_mcn_t, mcn), (const char *)mcn.medium_catalog_number, MIN(sizeof(atari_mcn->mcn), sizeof(mcn.medium_catalog_number)));
			}
			break;

		case ATARI_CDROMGETTISRC:
			D(bug(NFCD_NAME " Ioctl(CDROMGETTISRC) (NI)"));
			break;

		default:
			D(bug(NFCD_NAME " Ioctl(0x%04x) (NI)", opcode));
			break;
	}
	CloseDrive(drive);
	return errorcode;
}

int32 CdromDriverLinux::cd_startaudio(memptr device, uint32 dummy, memptr buffer)
{
	UNUSED(dummy);
	int drive, errorcode;
	struct cdrom_ti	track_index;
	metados_bos_tracks_t	*atari_track_index;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	atari_track_index = (metados_bos_tracks_t *) Atari2HostAddr(buffer);
	track_index.cdti_trk0 = atari_track_index->first;
	track_index.cdti_ind0 = 1;
	track_index.cdti_trk1 = atari_track_index->first + atari_track_index->count -1;
	track_index.cdti_ind0 = 99;

	errorcode=cd_unixioctl(drive, CDROMPLAYTRKIND, &track_index);
	CloseDrive(drive);
	return errorcode;
}

int32 CdromDriverLinux::cd_stopaudio(memptr device)
{
	int drive, errorcode;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	errorcode=cd_unixioctl(drive, CDROMSTOP, NULL);
	CloseDrive(drive);
	return errorcode;
}

int32 CdromDriverLinux::cd_setsongtime(memptr device, uint32 dummy, uint32 start_msf, uint32 end_msf)
{
	UNUSED(dummy);
	int drive, errorcode;
	struct cdrom_msf audio_bloc;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	audio_bloc.cdmsf_min0 = (start_msf>>16) & 0xff;
	audio_bloc.cdmsf_sec0 = (start_msf>>8) & 0xff;
	audio_bloc.cdmsf_frame0 = (start_msf>>0) & 0xff;
	D(bug(NFCD_NAME " start: %02d:%02d:%02d", (start_msf>>16) & 0xff, (start_msf>>8) & 0xff, start_msf & 0xff));

	audio_bloc.cdmsf_min1 = (end_msf>>16) & 0xff;
	audio_bloc.cdmsf_sec1 = (end_msf>>8) & 0xff;
	audio_bloc.cdmsf_frame1 = (end_msf>>0) & 0xff;
	D(bug(NFCD_NAME " end:   %02d:%02d:%02d", (end_msf>>16) & 0xff, (end_msf>>8) & 0xff, end_msf & 0xff));

	errorcode=cd_unixioctl(drive, CDROMPLAYMSF, &audio_bloc);
	CloseDrive(drive);
	return errorcode;
}

int32 CdromDriverLinux::cd_gettoc(memptr device, uint32 dummy, memptr buffer)
{
	UNUSED(dummy);
	int drive, i, numtracks, errorcode;
	struct cdrom_tochdr tochdr;
	struct cdrom_tocentry tocentry;
	atari_tocentry_t *atari_tocentry;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	/* Read TOC header */
	errorcode=cd_unixioctl(drive, CDROMREADTOCHDR, &tochdr);
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}

	D(bug(NFCD_NAME "GetToc():  TOC header read"));

	numtracks = tochdr.cdth_trk1-tochdr.cdth_trk0+1;

	/* Read TOC entries */
	atari_tocentry = (atari_tocentry_t *) Atari2HostAddr(buffer);
	for (i=0; i<=numtracks; i++) {
		D(bug(NFCD_NAME "GetToc():  reading TOC entry for track %d", i));

		tocentry.cdte_track = tochdr.cdth_trk0+i;
		if (i == numtracks) {
			tocentry.cdte_track = CDROM_LEADOUT;
		}
		tocentry.cdte_format = CDROM_MSF;

		errorcode = cd_unixioctl(drive, CDROMREADTOCENTRY, &tocentry);
		if (errorcode<0) {
			CloseDrive(drive);
			return errorcode;
		}

		atari_tocentry[i].track = tocentry.cdte_track;
		atari_tocentry[i].minute = BinaryToBcd(tocentry.cdte_addr.msf.minute);
		atari_tocentry[i].second = BinaryToBcd(tocentry.cdte_addr.msf.second);
		atari_tocentry[i].frame = BinaryToBcd(tocentry.cdte_addr.msf.frame);
		if (tocentry.cdte_track == CDROM_LEADOUT) {
			atari_tocentry[i].track = CDROM_LEADOUT_CDAR;
		}

		D(bug(NFCD_NAME "GetToc():   track %d, id 0x%02x, %s",
			i, tocentry.cdte_track,
			tocentry.cdte_ctrl & CDROM_DATA_TRACK ? "data" : "audio"
		));

		if (tocentry.cdte_format == CDROM_MSF) {
			D(bug(NFCD_NAME "GetToc():    msf %02d:%02d:%02d, bcd: %02x:%02x:%02x",
				tocentry.cdte_addr.msf.minute,
				tocentry.cdte_addr.msf.second,
				tocentry.cdte_addr.msf.frame,
				atari_tocentry[i].minute,
				atari_tocentry[i].second,
				atari_tocentry[i].frame
			));
		} else {
			D(bug(NFCD_NAME "GetToc():    lba 0x%08x", tocentry.cdte_addr.lba));
		}
	}

	atari_tocentry[numtracks+1].track =
		atari_tocentry[numtracks+1].minute =
		atari_tocentry[numtracks+1].second = 
		atari_tocentry[numtracks+1].frame = 0;

	CloseDrive(drive);
	return TOS_E_OK;
}

int32 CdromDriverLinux::cd_discinfo(memptr device, memptr buffer)
{
	int drive, errorcode;
	struct cdrom_tochdr tochdr;
	struct cdrom_subchnl subchnl;
	struct cdrom_tocentry tocentry;
	atari_discinfo_t	*discinfo;

	drive = OpenDrive(device);
	if (drive<0) {
		return drive;
	}

	/* Read TOC header */
	errorcode=cd_unixioctl(drive, CDROMREADTOCHDR, &tochdr);
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}
	D(bug(NFCD_NAME "DiscInfo():  TOC header read"));

	discinfo = (atari_discinfo_t *) Atari2HostAddr(buffer);
	memset(discinfo, 0, sizeof(atari_discinfo_t));

	discinfo->first = discinfo->current = tochdr.cdth_trk0;
	discinfo->last = tochdr.cdth_trk1;
	discinfo->index = 1;

	/* Read subchannel */
	subchnl.cdsc_format = CDROM_MSF;
	errorcode=cd_unixioctl(drive, CDROMSUBCHNL, &subchnl);
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}
	D(bug(NFCD_NAME "DiscInfo():  Subchannel read"));

	discinfo->current = subchnl.cdsc_trk;	/* current track */
	discinfo->index = subchnl.cdsc_ind;	/* current index */

	discinfo->relative.track = 0;
	discinfo->relative.minute = BinaryToBcd(subchnl.cdsc_reladdr.msf.minute);
	discinfo->relative.second = BinaryToBcd(subchnl.cdsc_reladdr.msf.second);
	discinfo->relative.frame = BinaryToBcd(subchnl.cdsc_reladdr.msf.frame);

	discinfo->absolute.track = 0;
	discinfo->absolute.minute = BinaryToBcd(subchnl.cdsc_absaddr.msf.minute);
	discinfo->absolute.second = BinaryToBcd(subchnl.cdsc_absaddr.msf.second);
	discinfo->absolute.frame = BinaryToBcd(subchnl.cdsc_absaddr.msf.frame);

	/* Read toc entry for start of disc, to select disc type */
	tocentry.cdte_track = tochdr.cdth_trk0;
	tocentry.cdte_format = CDROM_MSF;

	errorcode = cd_unixioctl(drive, CDROMREADTOCENTRY, &tocentry);
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}
	discinfo->disctype = ((tocentry.cdte_ctrl & CDROM_DATA_TRACK) == CDROM_DATA_TRACK);

	/* Read toc entry for end of disc */
	tocentry.cdte_track = CDROM_LEADOUT;
	tocentry.cdte_format = CDROM_MSF;

	errorcode = cd_unixioctl(drive, CDROMREADTOCENTRY, &tocentry);
	if (errorcode<0) {
		CloseDrive(drive);
		return errorcode;
	}

	D(bug(NFCD_NAME "DiscInfo():  Toc entry read"));

	discinfo->end.track = 0;
	discinfo->end.minute = BinaryToBcd(tocentry.cdte_addr.msf.minute);
	discinfo->end.second = BinaryToBcd(tocentry.cdte_addr.msf.second);
	discinfo->end.frame = BinaryToBcd(tocentry.cdte_addr.msf.frame);

	D(bug(NFCD_NAME "DiscInfo():  first=%d, last=%d, current=%d, ind=%d, rel=%02x:%02x:%02x, abs=%02x:%02x:%02x, end=%02x:%02x:%02x",
		discinfo->first, discinfo->last, discinfo->current, discinfo->index,
		discinfo->relative.minute, discinfo->relative.second, discinfo->relative.frame,
		discinfo->absolute.minute, discinfo->absolute.second, discinfo->absolute.frame,
		discinfo->end.minute, discinfo->end.second, discinfo->end.frame));

	CloseDrive(drive);
	return TOS_E_OK;
}

/*
vim:ts=4:sw=4:
*/
