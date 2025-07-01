using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;

namespace TcpClientForEsp
{
    class Program
    {
        private static volatile bool _isRunning = true;
        private static TcpClient _client = null;
        private static NetworkStream _stream = null;

        static void Main(string[] args)
        {
            Console.WriteLine("TCP Client for ESP8266 (.NET Framework 4.8)");
            Console.WriteLine("Нажмите Ctrl+C или введите 'exit' для выхода");

            Console.CancelKeyPress += (sender, e) =>
            {
                e.Cancel = true;
                CleanupAndExit();
            };

            string ipAddress = "192.168.0.108";
            int port = 502;

            while (_isRunning)
            {
                try
                {
                    _client = new TcpClient();
                    _client.SendTimeout = 3000;
                    _client.ReceiveTimeout = 3000;

                    Console.WriteLine($"Попытка подключения к {ipAddress}:{port}...");
                    _client.Connect(ipAddress, port);

                    if (_client.Connected)
                    {
                        Console.WriteLine("Подключение успешно!");
                        _stream = _client.GetStream();
                        HandleCommunication();
                    }
                    else
                    {
                        Console.WriteLine("Не удалось подключиться!");
                    }
                }
                catch (SocketException ex)
                {
                    Console.WriteLine($"Сетевая ошибка: {ex.SocketErrorCode}. {ex.Message}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Ошибка: {ex.Message}");
                }
                finally
                {
                    CleanupResources();
                }

                if (_isRunning)
                {
                    Console.WriteLine("Повторная попытка подключения через 3 секунды...");
                    Thread.Sleep(3000);
                }
            }

            CleanupAndExit();
        }

        private static void HandleCommunication()
        {
            Console.WriteLine("Введите шестнадцатеричные байты через пробел (например, 12 34 56)");
            Console.WriteLine("или 'exit' для выхода:");

            while (_isRunning && _client != null && IsConnected(_client))
            {
                try
                {
                    Console.Write("> ");
                    string input = Console.ReadLine();

                    if (string.IsNullOrWhiteSpace(input))
                        continue;

                    if (input.ToLower() == "exit")
                    {
                        CleanupAndExit();
                        return;
                    }

                    byte[] data = ParseHexString(input);
                    if (data.Length == 0)
                    {
                        Console.WriteLine("Ошибка: неверный формат ввода");
                        continue;
                    }

                    // Проверка соединения перед отправкой
                    if (!IsConnected(_client))
                    {
                        Console.WriteLine("Соединение потеряно перед отправкой. Попытка переподключения...");
                        return;
                    }

                    _stream.Write(data, 0, data.Length);
                    _stream.Flush();
                    Console.WriteLine($"Отправлено: {BitConverter.ToString(data).Replace("-", " ")}");

                    if (WaitForData(_stream, TimeSpan.FromSeconds(2)))
                    {
                        byte[] responseData = new byte[_client.ReceiveBufferSize];
                        int bytesRead = _stream.Read(responseData, 0, responseData.Length);

                        if (bytesRead > 0)
                        {
                            string response = BitConverter.ToString(responseData, 0, bytesRead);
                            Console.WriteLine($"Получено: {response.Replace("-", " ")}");
                        }
                        else
                        {
                            Console.WriteLine("Ответ не получен (разрыв соединения)");
                            return;
                        }
                    }
                    else
                    {
                        Console.WriteLine("Ответ не получен (таймаут). Отключение и попытка переподключения...");
                        CleanupResources();
                        return;
                    }
                }
                catch (IOException ex)
                {
                    Console.WriteLine($"Ошибка ввода-вывода (возможно, разрыв соединения): {ex.Message}");
                    return;
                }
                catch (SocketException ex)
                {
                    Console.WriteLine($"Сетевая ошибка: {ex.SocketErrorCode}. {ex.Message}");
                    return;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Ошибка обработки команды: {ex.Message}");
                }
            }

            Console.WriteLine("Соединение потеряно.");
        }

        private static void CleanupResources()
        {
            try
            {
                if (_stream != null)
                {
                    _stream.Close();
                    _stream = null;
                }

                if (_client != null)
                {
                    _client.Close();
                    _client = null;
                }
            }
            catch { }
        }

        private static void CleanupAndExit()
        {
            if (!_isRunning) return;

            _isRunning = false;
            Console.WriteLine("Завершение работы...");
            CleanupResources();
        }

        private static bool IsConnected(TcpClient client)
        {
            if (client == null || !client.Connected)
                return false;

            try
            {
                Socket socket = client.Client;
                // Проверка: если сокет доступен для чтения и доступных байт = 0, соединение закрыто
                if (socket.Poll(0, SelectMode.SelectRead) && socket.Available == 0)
                    return false;

                // Дополнительная проверка: пинг сокета (небольшой write)
                if (!socket.Connected)
                    return false;

                return true;
            }
            catch
            {
                return false;
            }
        }


        private static byte[] ParseHexString(string input)
        {
            input = input.Replace(" ", "").ToUpper();

            if (input.Length % 2 != 0)
                throw new FormatException("Длина строки должна быть четной (по 2 символа на байт)");

            byte[] result = new byte[input.Length / 2];
            for (int i = 0; i < result.Length; i++)
            {
                string byteStr = input.Substring(i * 2, 2);
                if (!byte.TryParse(byteStr, System.Globalization.NumberStyles.HexNumber, null, out result[i]))
                {
                    throw new FormatException($"Неверный байт: {byteStr}");
                }
            }

            return result;
        }

        private static bool WaitForData(NetworkStream stream, TimeSpan timeout)
        {
            DateTime start = DateTime.Now;
            while (DateTime.Now - start < timeout)
            {
                if (stream.DataAvailable)
                {
                    return true;
                }
                Thread.Sleep(50);
            }
            return false;
        }
    }
}
