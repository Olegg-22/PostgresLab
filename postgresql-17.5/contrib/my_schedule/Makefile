# Имя расширения
EXTENSION = pg_my_schedule

# Путь к исходным файлам
DATA = pg_my_schedule--1.0.sql

#
MODULE_big = my_schedule
OBJS = my_schedule.o

# Путь к файлу с описанием расширения
PG_CONFIG = /home/olegg/summer_school_PG/pg17_install/bin/pg_config

# Путь к бинарному файлу
#MODULES = my_schedule

# Путь к файлу с описанием расширения
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)
