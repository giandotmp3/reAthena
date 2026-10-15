-- Legacy rows for the rathena-migrate import CI job (.github/workflows/build_reimagined_units.yml).
-- Loaded over a latin1 connection after sql-files/main.sql and logs.sql. Covers raw EUC-KR /
-- CP1252 bytes (hex literals, so they are stored untouched), a blob, MySQL zero dates and an
-- invalid enum value and an AUTO_INCREMENT above max(id) that the target's counter must continue from.
SET SESSION sql_mode = '';
INSERT INTO `login` (`account_id`, `userid`, `user_pass`, `sex`, `email`, `lastlogin`, `birthdate`) VALUES
  (2000000, 'hero', 'pw1', 'M', 'a@example.invalid', '0000-00-00 00:00:00', '0000-00-00'),
  (2000001, X'C7D1B1DB', 'pw2', 'F', 'b@example.invalid', '2024-05-01 10:20:30', '1990-02-03'),
  (2000002, 'badsex', 'pw3', 'X', 'c@example.invalid', NULL, NULL); -- non-strict MySQL stores '' for 'X'
INSERT INTO `char` (`char_id`, `account_id`, `char_num`, `name`, `last_login`) VALUES
  (150000, 2000000, 0, X'4A6F73E9', '0000-00-00 00:00:00'),
  (150001, 2000001, 0, X'C7D1B1DBB0FA', '2024-05-01 10:20:30');
INSERT INTO `guild` (`guild_id`, `name`, `char_id`, `master`, `emblem_data`) VALUES (11, X'47C7D1', 150000, X'4A6F73E9', X'00010280FF');
INSERT INTO `mail` (`id`, `send_name`, `dest_id`, `title`, `message`) VALUES (1, 'gm', 150000, 'hi', X'E9E8'), (2, 'gm', 150001, 'two', 'x');
INSERT INTO `mail_attachments` (`id`, `index`, `nameid`, `amount`) VALUES (1, 0, 501, 1), (2, 0, 502, 3);
INSERT INTO `zenylog` (`time`, `char_id`, `src_id`, `type`, `amount`, `map`) VALUES ('0000-00-00 00:00:00', 150000, 0, 'S', 100, 'prontera');
INSERT INTO `picklog` (`time`, `char_id`, `type`, `nameid`, `amount`, `map`) VALUES ('2024-01-02 03:04:05', 150001, 'P', 501, 1, 'prontera');
ALTER TABLE `char` AUTO_INCREMENT = 150050;
