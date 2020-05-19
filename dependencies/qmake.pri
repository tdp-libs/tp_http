
custom_boost{
  # Custom bost config has been specified in project.inc
  # CONFIG += custom_boost
}else:win32{
  LIBS    += -llibboost_system
  LIBS    += -llibcrypto64MT
  LIBS    += -llibssl64MT
}else{
  LIBS    += -lboost_system
  LIBS    += -lssl
  LIBS    += -lcrypto
}
