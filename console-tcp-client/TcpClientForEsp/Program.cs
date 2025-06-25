using System.Net.Sockets;

namespace TcpClientForEsp;

class Program
{
    private static volatile bool _isRunning = true;
    private static TcpClient _client = null;
    private static NetworkStream _stream = null;

    static void Main(string[] args)
    {
        Console.WriteLine("TCP Client for ESP8266");
        Console.WriteLine("Нажмите Ctrl+C или введите 'exit' для выхода");

        // Обработка Ctrl+C для корректного завершения
        Console.CancelKeyPress += (sender, e) =>
        {
            e.Cancel = true; // Предотвращаем немедленное завершение процесса
            CleanupAndExit();
        };

        string ipAddress = "192.168.0.108";
        int port = 502;

        try
        {
            // Бесконечный цикл подключения
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
                    // Очищаем ресурсы перед следующей попыткой подключения
                    CleanupResources();
                }

                // Пауза перед повторной попыткой, если приложение еще работает
                if (_isRunning)
                {
                    Console.WriteLine("Повторная попытка подключения...");                    
                }
            }
        }
        finally
        {
            CleanupAndExit();
        }
    }
    private static void HandleCommunication()
    {
        Console.WriteLine("Введите шестнадцатеричные байты через пробел (например, 12 34 56)");
        Console.WriteLine("или 'exit' для выхода:");

        while (_isRunning && _client != null && _client.Connected)
        {
            try
            {
                Console.Write("> ");
                string input = Console.ReadLine();

                if (string.IsNullOrEmpty(input))
                    continue;

                if (input.ToLower() == "exit")
                {
                    CleanupAndExit();
                    return;
                }

                // Проверка подключения перед отправкой
                if (!IsConnected(_client))
                {
                    Console.WriteLine("Соединение потеряно. Попытка переподключения...");
                    return; // Выход из метода, чтобы переподключиться
                }

                byte[] data = ParseHexString(input);
                if (data.Length == 0)
                {
                    Console.WriteLine("Ошибка: неверный формат ввода");
                    continue;
                }

                // Отправляем данные
                _stream.Write(data, 0, data.Length);
                Console.WriteLine($"Отправлено: {BitConverter.ToString(data).Replace("-", " ")}");

                // Получаем ответ
                if (WaitForData(_stream, TimeSpan.FromSeconds(2)))
                {
                    byte[] responseData = new byte[_client.ReceiveBufferSize];
                    int bytesRead = _stream.Read(responseData, 0, responseData.Length);

                    if (bytesRead > 0)
                    {
                        string response = BitConverter.ToString(responseData, 0, bytesRead);
                        Console.WriteLine($"Получено: {response.Replace("-", " ")}");
                    }
                }
                else
                {
                    Console.WriteLine("Ответ не получен (таймаут)");
                }
            }
            catch (IOException ex)
            {
                Console.WriteLine($"Ошибка ввода-вывода (возможно, разрыв соединения): {ex.Message}");
                return; // Выйдем из метода, чтобы попытаться переподключиться
            }
            catch (SocketException ex)
            {
                Console.WriteLine($"Сетевая ошибка: {ex.SocketErrorCode}. {ex.Message}");
                return; // Выйдем из метода, чтобы попытаться переподключиться
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Ошибка обработки команды: {ex.Message}");
                // Продолжаем работу после других ошибок
            }
        }
    }

    private static void CleanupResources()
    {
        try
        {
            if (_stream != null)
            {
                _stream.Close();
                _stream.Dispose();
                _stream = null;
            }

            if (_client != null)
            {
                if (_client.Connected)
                    _client.Close();
                _client.Dispose();
                _client = null;
            }
        }
        catch { /* Игнорируем ошибки при очистке */ }
    }

    private static void CleanupAndExit()
    {
        if (!_isRunning) return; // Уже в процессе выхода

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
            if (client.Client.Poll(0, SelectMode.SelectRead))
            {
                byte[] buff = new byte[1];
                if (client.Client.Receive(buff, SocketFlags.Peek) == 0)
                {
                    return false;
                }
            }
            return true;
        }
        catch
        {
            return false;
        }
    }

    private static byte[] ParseHexString(string input)
    {
        input = input.Replace(" ", "").ToUpper(); // Удаляем пробелы и приводим к верхнему регистру

        if (input.Length % 2 != 0)
            throw new FormatException("Длина строки должна быть четной (по 2 символа на байт)");

        byte[] result = new byte[input.Length / 2];

        for (int i = 0; i < result.Length; i++)
        {
            string byteStr = input.Substring(i * 2, 2);
            if (!byte.TryParse(byteStr, System.Globalization.NumberStyles.HexNumber, null, out result[i]))
            {
                throw new FormatException($"Неверный формат шестнадцатеричного байта: {byteStr}");
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