"""Upload STM32 images through the ST-Link V2 SWD (dapdirect) transport.

PlatformIO's current ststm32 platform asks OpenOCD 0.12 for the removed
``hla_swd`` transport.  The stock ``stlink.cfg`` selects the supported SWD
transport itself, so use it directly for both project images.
"""

from os.path import join

Import("env")

FLASH_ADDRESS = {
    "bluepill": "0x08000000",
    "app": "0x08002000",
}[env.subst("$PIOENV")]

platform = env.PioPlatform()
openocd_dir = platform.get_package_dir("tool-openocd")
openocd = join(openocd_dir, "bin", "openocd.exe")
scripts_dir = join(openocd_dir, "openocd", "scripts")
interface_cfg = join(scripts_dir, "interface", "stlink.cfg")
target_cfg = join(scripts_dir, "target", "stm32f1x.cfg")


def _upload_source_path(env, source):
    """OpenOCD Tcl requires forward slashes in Windows file paths."""
    return source.get_abspath().replace("\\", "/")


env.Replace(
    __stlink_upload_source_path=_upload_source_path,
    UPLOADCMD=(
        '"{openocd}" -f "{interface}" -f "{target}" '
        '-c "program ${{__stlink_upload_source_path(__env__, SOURCE)}} '
        '{address} verify reset exit"'
    ).format(
        openocd=openocd,
        interface=interface_cfg,
        target=target_cfg,
        address=FLASH_ADDRESS,
    )
)
