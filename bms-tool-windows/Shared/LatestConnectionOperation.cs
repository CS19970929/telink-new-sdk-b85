namespace BmsTool.Windows;

// 一个窗口只有一个连接状态所有者；后来的请求先取消并等待前一个请求清理完毕。
public sealed class LatestConnectionOperation
{
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _sync = new();
    private CancellationTokenSource? _current;
    public bool IsBusy { get { lock (_sync) return _current is not null; } }
    public void Cancel() { lock (_sync) _current?.Cancel(); }
    public async Task<T> RunAsync<T>(Func<CancellationToken, Task<T>> action, CancellationToken ct = default)
    {
        var own = CancellationTokenSource.CreateLinkedTokenSource(ct);
        lock (_sync) { _current?.Cancel(); _current = own; }
        bool entered = false;
        try
        {
            await _gate.WaitAsync(own.Token);
            entered = true;
            own.Token.ThrowIfCancellationRequested();
            T result = await action(own.Token);
            own.Token.ThrowIfCancellationRequested();
            return result;
        }
        finally
        {
            if (entered) _gate.Release();
            lock (_sync) { if (ReferenceEquals(_current, own)) _current = null; own.Dispose(); }
        }
    }
}
