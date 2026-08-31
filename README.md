# OPC → Modbus TCP 网关 使用帮助

## 【软件功能】

本软件从 OPC DA 服务器读取实时数据，并通过 Modbus TCP 协议转发，供其他支持 Modbus 的设备或系统采集。支持本地和远程 OPC 服务器连接，支持多种数据类型、字节序配置、多服务器、多客户端同时访问。

## 【基本操作】

1. 启动程序后，首先进入“通讯参数设置”界面。
2. 在此界面可手动添加 OPC 服务器（输入名称、IP、ProgID/CLSID、用户名、密码），也可通过“枚举”按钮搜索远程或本地的 OPC 服务器列表并添加。
3. 已添加的服务器会显示在右侧列表中，可随时删除。
4. 设置 Modbus 端口、从站 ID、刷新间隔，并选择映射文件 (mapping.csv)。
5. 点击“保存并启动”，主窗口显示实时数据表格，Modbus TCP 服务器开始监听。
6. 关闭主窗口可最小化到系统托盘，右键托盘图标可显示窗口或退出。
7. 点击主窗口右下角“帮助”按钮可随时查看本文档。

注意：加密按钮可用于程序及 servers.csv 映射文件转移至其他设备或本机登录用户名变更后，重新输入 OPC 明文使用加密功能获得新密码，将新密码填入原 servers.csv 替换旧密码。

## 【映射文件 (mapping.csv) 编制要求】

- 文件格式：CSV (逗号分隔)，ANSI 编码，第一行为表头，之后每行定义一个点位。
- 表头及列说明：

| 列名          | 说明                                                                                                 |
|---------------|------------------------------------------------------------------------------------------------------|
| ServerName    | 必须与 servers.csv 中的服务器名称一致。                                                              |
| OPCItemID     | OPC 服务器中的标签路径（如 Channel1.Device1.Tag1）。                                                 |
| DataType      | 数据类型，支持以下值（区分大小写）：<br> - Bool → 布尔量，映射到 Modbus 线圈 (0 区)<br> - Word → 16 位无符号整数<br> - Int → 16 位有符号整数<br> - DWord → 32 位无符号整数<br> - Long → 32 位有符号整数<br> - Float → 32 位单精度浮点数<br> - Double → 64 位双精度浮点数 |
| ModbusAddr    | Modbus 起始地址（0-based）。<br> - 对于 Bool 类型，此地址为线圈地址；<br> - 对于 Word/Int 类型，地址占用 1 个寄存器；<br> - 对于 DWord/Long/Float，地址占用 2 个寄存器，仅填写首地址；<br> - 对于 Double，地址占用 4 个寄存器，仅填写首地址。 |
| ByteOrder     | 多字节/多字排列顺序，仅对 DWord/Long/Float/Double 有效。Word/Int/Bool 可留空或填任意值。<br>支持格式：<br> - ABCD → 大端模式（默认），高字节在低地址<br> - CDAB → 中间交换<br> - BADC → 另一交换<br> - DCBA → 小端模式<br>对于 Double 类型，类似地使用 8 字母组合，如 ABCDEFGH。 |

示例：
```csv
    ServerName,OPCItemID,DataType,ModbusAddr,ByteOrder
    Kepware,Channel1.Device1.Tag1,Word,0,
    Kepware,Channel1.Device1.FloatTag,Float,2,CDAB
```
## 【OPC 服务器配置 (servers.csv) 编制要求】

- 文件格式：CSV，ANSI 编码，第一行为表头。
- 表头及列说明：

| 列名          | 说明                                                                                                 |
|---------------|------------------------------------------------------------------------------------------------------|
| ServerName    | 自定义服务器名称，用于在 mapping.csv 中引用。                                                        |
| IPAddress     | OPC 服务器的 IP 地址。本地填 127.0.0.1。                                                             |
| ProgID        | OPC 服务器的 ProgID 或 CLSID 字符串。本地连接可直接使用 ProgID；远程连接建议使用 CLSID（可通过“枚举”按钮获取远程服务器的 ProgID 和 CLSID）。 |
| UserName      | 登录远程计算机的用户名。本地可留空。                                                                 |
| Password      | 对应密码，支持明文或 ENC: 开头的加密密文。                                                           |
| CLSID         | 可选，显式指定服务器的 CLSID，优先于 ProgID 使用。                                                   |

 示例：
```csv
   ServerName,IPAddress,ProgID,UserName,Password,CLSID
   LocalKEP,127.0.0.1,KEPware.KEPServerEx.V4,,,
   RemoteKEP,192.168.1.100,{7BC0CC8E-482C-47CA-ABDC-0FE7F9C6E729},admin,ENC:...
```
## 【特殊应用场景：mapping2.csv】

若程序目录下存在 mapping2.csv 文件且包含数据行，程序将自动启用第二从站（从站 ID = 主从站 ID + 1），并对指定的 Word 类型标签进行位解析和浮点转发。

- 文件格式：CSV，ANSI 编码，第一行为表头，之后每行定义一个解析条目。
- 表头及列说明：

| 列名          | 说明                                                                                                 |
|---------------|------------------------------------------------------------------------------------------------------|
| ServerName    | 必须与 servers.csv 中的服务器名称一致。                                                              |
| OPCItemID     | OPC 服务器中的 Word 类型标签路径。                                                                   |
| ModbusAddr    | Modbus 保持寄存器首地址（0-based），浮点结果占用 2 个寄存器。                                          |
| Factor        | 计算因子。bit11-0 的整数值 × Factor 得到最终浮点数。                                                   |
| ByteOrder     | 浮点数的字节序，支持 ABCD（默认）、CDAB、BADC、DCBA。                                                 |

解析规则：
- 读取到的 Word 值的 bit15-12 分别作为 4 个线圈输出，线圈地址为：
  - base = ModbusAddr / 2 * 5
  - bit15 → 线圈 base+0
  - bit14 → 线圈 base+1
  - bit13 → 线圈 base+2
  - bit12 → 线圈 base+3
- 通讯状态位位于线圈 base+4：成功读取 OPC 数据时为 1，失败时为 0。
- bit11-0（0~4095）作为整型，乘以 Factor 后转为浮点数，按 ByteOrder 存入保持寄存器 ModbusAddr 和 ModbusAddr+1。

启用条件：
- 文件 mapping2.csv 必须存在于程序目录，且至少包含一条有效数据行。
- 若文件不存在或只有表头，此功能自动禁用，不影响主从站运行。

## 【注意事项】

- 映射文件中的标签路径必须与 OPC 服务器中完全一致，建议先用“导出条目”功能获取完整列表。
- 映射文件 mapping.csv 可通过调出通讯参数设置界面选择文件后热加载执行。
- 映射文件 mapping2.csv 必须放在程序同一目录且不支持热加载，修改文件后必须重启程序方能执行。
- Modbus 从站 ID 需与客户端设置一致，否则不会响应。通讯端口和 ID 改变后需重启程序。
- 最大支持 64 个 OPC 服务器连接。
- 程序退出时自动断开所有 OPC 连接并释放端口。
