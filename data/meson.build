desktops = [
  'org.mate.DiskUtility.desktop',
  'mate-disk-image-writer.desktop',
]

foreach desktop: desktops
  i18n.merge_file (
    desktop,
    type: 'desktop',
    input: desktop + '.in',
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: gdu_datadir / 'applications',
  )
endforeach


configure_file(
  input: 'org.mate.DiskUtility.service.in',
  output: '@BASENAME@',
  configuration: {'bindir': gdu_prefix / gdu_bindir},
  install: true,
  install_dir: gdu_datadir / 'dbus-1/services',
)

install_data(
  'org.mate.Disks.gschema.xml',
  install_dir: gio_schemasdir,
)

info = 'org.mate.DiskUtility.appdata.xml'

i18n.merge_file(
  info,
  input: info + '.in',
  output: info,
  po_dir: po_dir,
  install: true,
  install_dir: gdu_datadir / 'metainfo',
)
