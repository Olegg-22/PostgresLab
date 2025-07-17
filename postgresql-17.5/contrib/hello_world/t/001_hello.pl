use strict;
use warnings;

# Подключаем модули для тестов PostgreSQL
use PostgreSQL::Test::Cluster;
use PostgreSQL::Test::Utils;
use Test::More;

# Шаг 1. Создаём временный кластер PostgreSQL
my $node = PostgreSQL::Test::Cluster->new('hello_test');
$node->init;          # инициализация
$node->start;         # запуск сервера

# Шаг 2. Создаём расширение hello_world
$node->safe_psql('postgres', 'CREATE EXTENSION hello_world');

# Шаг 3. Проверка C-функции hello_world_c
my $result = $node->safe_psql('postgres', q{
    SELECT hello_world_c('Oleg');
});

# Удаляем лишние пробелы
chomp($result);

# Шаг 4. Проверяем результат
is($result, 'Hello from c, Oleg', 'C-функция работает корректно');

# Завершение
done_testing();
