# Имя расширения
EXTENSION = hello_world
REGRESS = hello_world
TAP_TESTS = 1
# Путь к исходным файлам
DATA = hello_world--1.0.sql

#
MODULE_big = hello_world
OBJS = hello_world.o

# Путь к файлу с описанием расширения
PG_CONFIG = /home/olegg/summer_school_PG/pg17_install/bin/pg_config

# Путь к бинарному файлу
#MODULES = hello_world

# Путь к файлу с описанием расширения
PGXS := $(shell $(PG_CONFIG) --pgxs)

include $(PGXS)

